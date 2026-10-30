*** Comments ***
Firmware updates over IR (docs/firmware-updates.md), on the release
bootloader and application: one emulated IRL2FA sends its signed image to
another, whose bootloader receives, checks and installs it; power cuts at
many points of the transfer and of the install; refusals; readout
protection on the first power-on with a healthy cell.

The images come from make_test_images.py (run.sh makes them): the shipped
bootloader with the public TEST key in place of the release key, and the
shipped application signed with it as v0.1.0 and v0.2.0, plus a v0.3.0
signed with a key the receivers do not trust.
Run with firmware/renode/run.sh. SPDX-License-Identifier: AGPL-3.0-or-later

*** Settings ***
Library           Collections
Library           irl2fa_helpers.py
Suite Setup       Load Models
Suite Teardown    Teardown
Test Setup        Reset Emulation
Test Teardown     Save Screens And Test Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${ELF}            ${CURDIR}/../build/stm32u0/irl2fa.elf
${BOOT_ELF}       ${CURDIR}/../build/stm32u0/boot.elf
${IMAGES}         ${CURDIR}/../build/renode-images
${OUT}            ${CURDIR}/../build/renode
${UNIX}           1800000000
${APP_SLOT}       0x08006800
${STORE_OFF}      0x2C000
${STORE_LEN}      0x14000
${V1}             0x000100
${V2}             0x000200
@{DEVICES}
&{DEBUG_ADDR}

*** Keywords ***
Load Models
    Setup
    Execute Command    include @${CURDIR}/models/STM32U0_System.cs
    Execute Command    include @${CURDIR}/models/SSD1680.cs
    Execute Command    include @${CURDIR}/models/RV3032.cs
    Execute Command    include @${CURDIR}/models/PowerMonitor.cs
    Execute Command    emulation SetGlobalQuantum "0.0001"

Create Device
    [Arguments]    ${name}    ${seed}    ${image}    ${vdda}=3000
    Execute Command    mach create "${name}"
    Execute Command    machine LoadPlatformDescription @${CURDIR}/irl2fa.repl
    Execute Command    $rngseed=${seed}
    Execute Command    $unix=${UNIX}
    Execute Command    include @${CURDIR}/board_init.resc
    Execute Command    sysbus.adc VddaMillivolts ${vdda}
    Execute Command    $factory=@${IMAGES}/${image}.bin
    Execute Command    $bootelf=@${BOOT_ELF}
    Execute Command    $appelf=@${ELF}
    Execute Command    include @${CURDIR}/load_firmware.resc
    ${addr}=    Execute Command    sysbus GetSymbolAddress "irl2fa_debug"
    Set To Dictionary    ${DEBUG_ADDR}    ${name}=${addr.strip()}
    Append To List    ${DEVICES}    ${name}

Connect Over IR
    [Arguments]    @{names}
    Execute Command    emulation CreateUARTHub "ir"
    FOR    ${n}    IN    @{names}
        Execute Command    mach set "${n}"
        Execute Command    connector Connect sysbus.usart1 ir
    END

Run For
    [Arguments]    ${seconds}
    Execute Command    emulation RunFor "${seconds}"

Debug Block
    [Arguments]    ${name}
    Execute Command    mach set "${name}"
    ${raw}=    Execute Command    sysbus ReadBytes ${DEBUG_ADDR}[${name}] 40
    ${dbg}=    Decode Debug Block    ${raw}
    RETURN    ${dbg}

Boot Block
    [Arguments]    ${name}
    Execute Command    mach set "${name}"
    ${raw}=    Execute Command    sysbus ReadBytes 0x20009FD0 32
    ${b}=    Decode Boot Block    ${raw}
    RETURN    ${b}

Word
    [Arguments]    ${name}    ${addr}
    Execute Command    mach set "${name}"
    ${out}=    Execute Command    sysbus ReadDoubleWord ${addr}
    ${v}=    Monitor Int    ${out}
    RETURN    ${v}

Model Int
    [Arguments]    ${name}    ${what}
    Execute Command    mach set "${name}"
    ${out}=    Execute Command    ${what}
    ${v}=    Monitor Int    ${out}
    RETURN    ${v}

Store Hash
    [Arguments]    ${name}
    Execute Command    mach set "${name}"
    ${h}=    Execute Command    sysbus.flash_ctrl RegionHash ${STORE_OFF} ${STORE_LEN}
    RETURN    ${h.strip()}

Wait For Screen
    [Arguments]    ${name}    ${expected}    ${timeout_s}=10
    ${steps}=    Evaluate    int(${timeout_s} * 4)
    FOR    ${i}    IN RANGE    ${steps}
        ${dbg}=    Debug Block    ${name}
        IF    '${dbg}[screen]' == '${expected}' and ${dbg}[magic] == 0x47424449    RETURN
        Run For    0.25
    END
    ${dbg}=    Debug Block    ${name}
    Should Be Equal    ${dbg}[screen]    ${expected}    ${name}: screen is ${dbg}[screen] after ${timeout_s} s

Wait Until Display Idle
    [Arguments]    ${name}    ${timeout_s}=10
    ${steps}=    Evaluate    int(${timeout_s} * 10)
    FOR    ${i}    IN RANGE    ${steps}
        Execute Command    mach set "${name}"
        ${p}=    Execute Command    sysbus.spi1.epd Powered
        IF    'False' in '''${p}'''    RETURN
        Run For    0.1
    END
    Fail    ${name}: panel still powered after ${timeout_s} s

Press
    [Arguments]    ${name}    ${button}
    Execute Command    mach set "${name}"
    Execute Command    sysbus.gpioPortA.${button} Press
    Run For    0.08
    Execute Command    mach set "${name}"
    Execute Command    sysbus.gpioPortA.${button} Release
    Run For    0.15
    Wait Until Display Idle    ${name}

Presses
    [Arguments]    ${name}    ${button}    ${n}
    FOR    ${i}    IN RANGE    ${n}
        Press    ${name}    ${button}
    END

Open Firmware Update
    [Documentation]    Home -> Settings -> Firmware update (no PIN set).
    [Arguments]    ${name}
    Wait For Screen    ${name}    menu    timeout_s=30
    Wait Until Display Idle    ${name}
    Presses    ${name}    joyDown    4
    Press    ${name}    joyCenter
    Presses    ${name}    joyDown    5
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    menu

Start Receiving
    [Documentation]    Firmware update -> Receive an update: the app restarts into the receiver.
    [Arguments]    ${name}
    Open Firmware Update    ${name}
    Press    ${name}    joyCenter
    Wait For Boot Phase    ${name}    2    timeout_s=30

Start Sending
    [Documentation]    Firmware update -> Send it to another.
    [Arguments]    ${name}    ${expect}=update
    Open Firmware Update    ${name}
    Press    ${name}    joyDown
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    ${expect}

Wait For Boot Phase
    [Arguments]    ${name}    ${phase}    ${timeout_s}=30
    ${steps}=    Evaluate    int(${timeout_s} * 4)
    FOR    ${i}    IN RANGE    ${steps}
        ${b}=    Boot Block    ${name}
        IF    ${b}[phase] == ${phase}    RETURN
        Run For    0.25
    END
    ${b}=    Boot Block    ${name}
    Should Be Equal As Integers    ${b}[phase]    ${phase}    ${name}: bootloader phase ${b}[phase] after ${timeout_s} s

Wait For Received
    [Documentation]    Until the receiver's next offset passes `permille` of the payload.
    [Arguments]    ${name}    ${permille}    ${timeout_s}=300
    ${size}=    Word    ${name}    0x0801E818
    ${steps}=    Evaluate    int(${timeout_s} * 2)
    FOR    ${i}    IN RANGE    ${steps}
        ${b}=    Boot Block    ${name}
        IF    ${b}[rx_next] * 1000 >= ${size} * ${permille}    RETURN
        Run For    0.5
    END
    Fail    ${name}: received ${b}[rx_next] of ${size} bytes after ${timeout_s} s

Wait For Erases
    [Documentation]    Until the flash has erased `n` more pages than `since`
    ...    (returns the count), or the bootloader has started the
    ...    application, i.e. the install is over (returns -1).
    [Arguments]    ${name}    ${since}    ${n}    ${timeout_s}=60
    ${steps}=    Evaluate    int(${timeout_s} * 50)
    FOR    ${i}    IN RANGE    ${steps}
        ${e}=    Model Int    ${name}    sysbus.flash_ctrl PageErases
        IF    ${e} >= ${since} + ${n}    RETURN    ${e}
        ${b}=    Boot Block    ${name}
        IF    ${b}[phase] == 4    RETURN    ${-1}
        Run For    0.02
    END
    Fail    ${name}: only ${e} page erases after ${timeout_s} s

Cut Power
    [Documentation]    The cell pulled and put back: the flash operation under way
    ...    is torn (an ECC-bad double-word, or a half-erased page), the RAM
    ...    mailbox is lost, the next start is a power-on. (Only the mailbox:
    ...    Renode applies the reset when the emulation resumes, and a core
    ...    running on a zeroed stack until then would crash first.)
    [Arguments]    ${name}
    Execute Command    mach set "${name}"
    ${torn}=    Execute Command    sysbus.flash_ctrl TearLastOperation
    Log    ${name}: ${torn}
    Execute Command    sysbus.sram SetRange 0x9FF0 0x10 0x00
    Execute Command    sysbus.rcc PowerOnReset
    Execute Command    machine RequestReset
    Run For    0.001

Request Receiver
    [Documentation]    What "Receive an update" does: the mailbox, then a reset.
    [Arguments]    ${name}
    Execute Command    mach set "${name}"
    Execute Command    sysbus WriteDoubleWord 0x20009FF0 0x49324D42
    Execute Command    sysbus WriteDoubleWord 0x20009FF4 1
    Execute Command    sysbus WriteDoubleWord 0x20009FF8 0
    Execute Command    sysbus WriteDoubleWord 0x20009FFC 0xB6CDB2BC
    Execute Command    machine RequestReset
    Wait For Boot Phase    ${name}    2

Running Version Should Be
    [Documentation]    The application runs (debug block alive, bootloader handed over)
    ...    and its slot holds the image of that version.
    [Arguments]    ${name}    ${version}    ${timeout_s}=60
    ${steps}=    Evaluate    int(${timeout_s} * 4)
    FOR    ${i}    IN RANGE    ${steps}
        ${b}=    Boot Block    ${name}
        ${d}=    Debug Block    ${name}
        IF    ${b}[phase] == 4 and ${d}[magic] == 0x47424449 and ${d}[polls] > 0    BREAK
        Run For    0.25
    END
    Should Be Equal As Integers    ${b}[phase]    4    ${name}: the bootloader did not start the application
    ${v}=    Word    ${name}    0x08006808
    Should Be Equal As Integers    ${v}    ${version}    ${name}: running v${v}, expected v${version}

Resend From
    [Documentation]    The receiver was off for a while: the sender either found it
    ...    again by itself (its retries reach the restarted receiver, which
    ...    asks for an offer) or gave up, and is told to send again.
    [Arguments]    ${name}    ${receiver}
    FOR    ${i}    IN RANGE    160
        ${d}=    Debug Block    ${name}
        IF    '${d}[screen]' == 'message'    BREAK
        ${b}=    Boot Block    ${receiver}
        IF    ${b}[rx_state] == 1 and ${b}[rx_next] > 0    RETURN
        Run For    0.25
    END
    Wait For Screen    ${name}    message
    Wait Until Display Idle    ${name}
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    menu
    Press    ${name}    joyDown
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    update

Sleep And Wake
    [Documentation]    Left on the home menu sends it to sleep; the joystick wakes it.
    [Arguments]    ${name}
    Wait For Screen    ${name}    menu    timeout_s=30
    Wait Until Display Idle    ${name}
    Press    ${name}    joyLeft
    Run For    5
    Press    ${name}    joyCenter
    Run For    1

Dump Screen
    [Arguments]    ${name}    ${file}
    Execute Command    mach set "${name}"
    Execute Command    sysbus.spi1.epd DumpPng @${OUT}/${file}.png 3

Save Screens And Test Teardown
    ${test}=    Evaluate    re.sub(r'[^A-Za-z0-9]+', '-', $TEST_NAME).strip('-').lower()    modules=re
    FOR    ${name}    IN    @{DEVICES}
        Run Keyword And Ignore Error    Dump Screen    ${name}    last-${test}-${name}
    END
    ${empty}=    Create List
    Set Suite Variable    @{DEVICES}    @{empty}
    Test Teardown

*** Test Cases ***
Updates Over IR Through Power Cuts In Every Phase
    [Documentation]    bob (v0.1.0) receives v0.2.0 from alice. The cell is pulled
    ...    three times while it arrives and four or more times while it installs; every
    ...    time bob comes back bootable (v0.1.0 before the install starts, the
    ...    install finishing on its own after), the transfer resumes where it
    ...    stopped, and bob's record store is never touched.
    Create Device    alice    1    v2
    Create Device    bob    2    v1
    Connect Over IR    alice    bob
    Running Version Should Be    alice    ${V2}
    Running Version Should Be    bob    ${V1}
    Wait For Screen    bob    menu    timeout_s=30
    ${store}=    Store Hash    bob
    Start Receiving    bob
    Dump Screen    bob    update-receiving
    Start Sending    alice
    Dump Screen    alice    update-sending
    # ---- receive: cut at 10 %, 45 % and 80 % ----
    FOR    ${at}    IN    100    450    800
        Wait For Received    bob    ${at}
        Cut Power    bob
        Running Version Should Be    bob    ${V1}
        ${h}=    Store Hash    bob
        Should Be Equal    ${h}    ${store}    the record store changed
        Request Receiver    bob
        Resend From    alice    bob
        Run For    3
        ${after}=    Boot Block    bob
        ${floor}=    Evaluate    ${at} * 47000 // 1000 - 2048
        Should Be True    ${after}[rx_next] > ${floor}    the transfer did not resume where it stopped
    END
    # ---- install: cut after page erases 3, 9 and 20 of it, and every 4
    # after that until it gets through (the first erases may be the journal
    # page, before the install is journalled; pages the two builds share are
    # not rewritten) ----
    Wait For Boot Phase    bob    3    timeout_s=300
    ${e}=    Model Int    bob    sysbus.flash_ctrl PageErases
    ${cuts}=    Set Variable    ${0}
    FOR    ${n}    IN    3    6    11    4    4    4    4    4    4    4
        ${e}=    Wait For Erases    bob    ${e}    ${n}
        IF    ${e} < 0    BREAK
        ${cuts}=    Evaluate    ${cuts} + 1
        Run For    0.005
        Cut Power    bob
        ${h}=    Store Hash    bob
        Should Be Equal    ${h}    ${store}    the record store changed
        Run For    0.05
        ${e}=    Model Int    bob    sysbus.flash_ctrl PageErases
    END
    Should Be True    ${cuts} >= 4    only ${cuts} cuts during the install
    Running Version Should Be    bob    ${V2}    timeout_s=120
    ${h}=    Store Hash    bob
    Should Be Equal    ${h}    ${store}    the record store changed
    ${ecc}=    Model Int    bob    sysbus.flash_ctrl EccDetections
    Should Be True    ${ecc} >= 1    no torn double-word was ever read back
    # The install that finished after the last cut is reported, then the menu.
    Wait For Screen    bob    message    timeout_s=30
    Wait Until Display Idle    bob
    Dump Screen    bob    update-done
    Press    bob    joyCenter
    Wait For Screen    bob    menu
    # alice gave up on the last attempt (bob went quiet mid-install): fine.

Updates Over IR And Reports It
    [Documentation]    The plain path, start to end: alice sends, bob installs and
    ...    restarts into v0.2.0 and says so; alice says it was sent.
    Create Device    alice    1    v2
    Create Device    bob    2    v1
    Connect Over IR    alice    bob
    Wait For Screen    bob    menu    timeout_s=30
    ${store}=    Store Hash    bob
    Start Receiving    bob
    Start Sending    alice
    Wait For Boot Phase    bob    3    timeout_s=300
    Running Version Should Be    bob    ${V2}    timeout_s=120
    Wait For Screen    bob    message    timeout_s=30
    Wait Until Display Idle    bob
    Dump Screen    bob    update-reported
    Wait For Screen    alice    message    timeout_s=30
    Wait Until Display Idle    alice
    Dump Screen    alice    update-sent
    ${h}=    Store Hash    bob
    Should Be Equal    ${h}    ${store}    the record store changed

Refuses An Image Not Signed With Its Key
    Create Device    mallory    3    other
    Create Device    bob    2    v1
    Connect Over IR    mallory    bob
    Running Version Should Be    mallory    0x000300
    Wait For Screen    bob    menu    timeout_s=30
    Start Receiving    bob
    Start Sending    mallory
    Wait For Boot Phase    bob    3    timeout_s=300
    # The verdict, while the bootloader still tells the sender (it restarts after 6 s).
    FOR    ${i}    IN RANGE    200
        ${b}=    Boot Block    bob
        IF    ${b}[rx_state] == 4    BREAK
        Run For    0.1
    END
    Should Be Equal As Integers    ${b}[install_err]    6    expected FW_E_SIGNATURE
    Running Version Should Be    bob    ${V1}    timeout_s=60
    Wait For Screen    bob    message    timeout_s=30
    Wait For Screen    mallory    message    timeout_s=30
    Wait Until Display Idle    mallory
    Dump Screen    mallory    update-refused

Refuses An Older Image At Once
    Create Device    alice    1    v1
    Create Device    bob    2    v2
    Connect Over IR    alice    bob
    Wait For Screen    bob    menu    timeout_s=30
    Start Receiving    bob
    # Refused at the offer: "Not sent" before the sending screen even settles.
    Start Sending    alice    expect=message
    ${b}=    Boot Block    bob
    Should Be Equal As Integers    ${b}[rx_state]    4    the receiver did not refuse
    Should Be Equal As Integers    ${b}[rx_next]    0    an older image was accepted
    Should Be Equal As Integers    ${b}[install_err]    0    an install was attempted
    Running Version Should Be    bob    ${V2}    timeout_s=30

Sets Readout Protection When Its Owner First Wakes It On A Healthy Cell
    [Documentation]    Never on a programming fixture (power only, 3.0 V, power
    ...    cycles), never on a weak cell; once its owner wakes it with the
    ...    joystick on a fresh cell, level 1, once, and it carries on.
    Create Device    dev    1    v1
    Running Version Should Be    dev    ${V1}
    Cut Power    dev
    Running Version Should Be    dev    ${V1}
    ${rdp}=    Model Int    dev    sysbus.flash_ctrl RdpLevel
    Should Be Equal As Integers    ${rdp}    0    locked on the fixture
    # The owner wakes it, but the cell is weak: asked, declined.
    Execute Command    sysbus.adc VddaMillivolts 2700
    Sleep And Wake    dev
    Running Version Should Be    dev    ${V1}
    ${b}=    Boot Block    dev
    Should Be Equal As Integers    ${b}[rdp]    2
    ${rdp}=    Model Int    dev    sysbus.flash_ctrl RdpLevel
    Should Be Equal As Integers    ${rdp}    0
    # A fresh cell: the next start, the first wake.
    Execute Command    sysbus.adc VddaMillivolts 3000
    Cut Power    dev
    Running Version Should Be    dev    ${V1}
    Sleep And Wake    dev
    Running Version Should Be    dev    ${V1}
    ${rdp}=    Model Int    dev    sysbus.flash_ctrl RdpLevel
    Should Be Equal As Integers    ${rdp}    1
    ${n}=    Model Int    dev    sysbus.flash_ctrl OptionBytePrograms
    Should Be Equal As Integers    ${n}    1
    Wait For Screen    dev    menu    timeout_s=30
    # Once.
    Cut Power    dev
    Running Version Should Be    dev    ${V1}
    Sleep And Wake    dev
    ${n}=    Model Int    dev    sysbus.flash_ctrl OptionBytePrograms
    Should Be Equal As Integers    ${n}    1    option bytes written again

Never Protects A Development Image
    [Documentation]    The unsigned factory image (a build without the signing key)
    ...    boots only while the chip is unprotected, so it must never protect it.
    Create Device    dev    1    v1
    Running Version Should Be    dev    ${V1}
    Execute Command    mach set "dev"
    Execute Command    sysbus LoadBinary @${CURDIR}/../build/stm32u0/irl2fa-factory.bin 0x08000000
    Cut Power    dev
    Wait For Boot Phase    dev    4
    Sleep And Wake    dev
    Wait For Screen    dev    menu    timeout_s=30
    ${rdp}=    Model Int    dev    sysbus.flash_ctrl RdpLevel
    Should Be Equal As Integers    ${rdp}    0
    ${n}=    Model Int    dev    sysbus.flash_ctrl OptionBytePrograms
    Should Be Equal As Integers    ${n}    0
