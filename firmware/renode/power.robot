*** Comments ***
Measured power profile: the real STM32 firmware in Renode goes through a
scripted use (wake, contact words, TOTP codes, paging, idle to sleep) and a
pairing session, while models/PowerMonitor.cs records MCU Stop 2 / run time,
instructions, e-paper SPI traffic and BUSY time, RTC I2C traffic and IR on
time. The JSON timelines land in firmware/build/renode/power-*.json and
hardware/power/power.py --measured turns them into charge per use.
Run with firmware/renode/run.sh. SPDX-License-Identifier: AGPL-3.0-or-later

*** Settings ***
Library           Collections
Library           irl2fa_helpers.py
Suite Setup       Load Models
Suite Teardown    Teardown
Test Setup        Reset Emulation
Test Teardown     Test Teardown
Resource          ${RENODEKEYWORDS}

*** Variables ***
${ELF}            ${CURDIR}/../build/stm32u0/irl2fa.elf
${BOOT_ELF}       ${CURDIR}/../build/stm32u0/boot.elf
${FACTORY}        ${CURDIR}/../build/stm32u0/irl2fa-factory.bin
${OUT}            ${CURDIR}/../build/renode
${MODEL}          ${CURDIR}/../../hardware/power/model.json
${UNIX}           1800000000    # 2027-01-15 08:00:00 UTC
${GAP}            0.6           # seconds between a person's presses

*** Keywords ***
Load Models
    Setup
    Execute Command    include @${CURDIR}/models/STM32U0_System.cs
    Execute Command    include @${CURDIR}/models/SSD1680.cs
    Execute Command    include @${CURDIR}/models/RV3032.cs
    Execute Command    include @${CURDIR}/models/PowerMonitor.cs
    Execute Command    emulation SetGlobalQuantum "0.0001"

Create Device
    [Arguments]    ${name}    ${seed}
    Execute Command    mach create "${name}"
    Execute Command    machine LoadPlatformDescription @${CURDIR}/irl2fa.repl
    Execute Command    $rngseed=${seed}
    Execute Command    $unix=${UNIX}
    Execute Command    include @${CURDIR}/board_init.resc
    Execute Command    $factory=@${FACTORY}
    Execute Command    $bootelf=@${BOOT_ELF}
    Execute Command    $appelf=@${ELF}
    Execute Command    include @${CURDIR}/load_firmware.resc
    ${full}    ${partial}=    Power Model Epd Ms    ${MODEL}
    Execute Command    sysbus.spi1.epd FullRefreshMs ${full}
    Execute Command    sysbus.spi1.epd PartialRefreshMs ${partial}

Run For
    [Arguments]    ${seconds}
    Execute Command    emulation RunFor "${seconds}"

Debug Block
    [Arguments]    ${name}
    Execute Command    mach set "${name}"
    ${addr}=    Execute Command    sysbus GetSymbolAddress "irl2fa_debug"
    ${raw}=    Execute Command    sysbus ReadBytes ${addr} 40
    ${dbg}=    Decode Debug Block    ${raw}
    RETURN    ${dbg}

Wait For Screen
    [Arguments]    ${name}    ${expected}    ${timeout_s}=10
    ${steps}=    Evaluate    int(${timeout_s} * 10)
    FOR    ${i}    IN RANGE    ${steps}
        ${dbg}=    Debug Block    ${name}
        IF    '${dbg}[screen]' == '${expected}'    RETURN
        Run For    0.1
    END
    ${dbg}=    Debug Block    ${name}
    Should Be Equal    ${dbg}[screen]    ${expected}    ${name}: screen is ${dbg}[screen] after ${timeout_s} s

Wait Until Display Idle
    [Documentation]    Panel unpowered on two checks 0.15 s apart. One check
    ...    can fall between a refresh that held up a press and the refresh
    ...    that press causes; pressing again in that gap merges the presses
    ...    (the firmware has not yet seen the first one released).
    [Arguments]    ${name}    ${timeout_s}=10
    ${steps}=    Evaluate    int(${timeout_s} * 20)
    ${idle}=    Set Variable    ${0}
    FOR    ${i}    IN RANGE    ${steps}
        Execute Command    mach set "${name}"
        ${p}=    Execute Command    sysbus.spi1.epd Powered
        IF    'False' in '''${p}'''
            ${idle}=    Evaluate    ${idle} + 1
            IF    ${idle} >= 2    RETURN
            Run For    0.15
        ELSE
            ${idle}=    Set Variable    ${0}
            Run For    0.05
        END
    END
    Fail    ${name}: panel still powered after ${timeout_s} s

Wait For Sleep
    [Arguments]    ${name}    ${count}    ${timeout_s}=90
    ${steps}=    Evaluate    int(${timeout_s} * 4)
    FOR    ${i}    IN RANGE    ${steps}
        ${dbg}=    Debug Block    ${name}
        IF    ${dbg}[sleeps] >= ${count}    BREAK
        Run For    0.25
    END
    ${dbg}=    Debug Block    ${name}
    Should Be True    ${dbg}[sleeps] >= ${count}    ${name}: still awake after ${timeout_s} s
    # Let the sleep screen's refresh finish and the core reach Stop 2.
    Wait Until Display Idle    ${name}
    Run For    0.5

Press
    [Arguments]    ${name}    ${button}
    Execute Command    mach set "${name}"
    Execute Command    sysbus.gpioPortA.${button} Press
    Run For    0.08
    Execute Command    mach set "${name}"
    Execute Command    sysbus.gpioPortA.${button} Release
    Run For    0.15
    Wait Until Display Idle    ${name}

Tap
    [Documentation]    A press at a person's pace: the press, then a pause.
    [Arguments]    ${name}    ${button}    ${expect}=${EMPTY}
    Press    ${name}    ${button}
    Run For    ${GAP}
    IF    '${expect}' != ''    Wait For Screen    ${name}    ${expect}

Presses
    [Arguments]    ${name}    ${button}    ${n}
    FOR    ${i}    IN RANGE    ${n}
        Press    ${name}    ${button}
    END

Power
    [Arguments]    ${name}    ${command}
    Execute Command    mach set "${name}"
    Execute Command    sysbus.power ${command}

Add Code
    [Documentation]    Home menu -> "Add a code", a one-letter label, the
    ...    16-character base32 secret AAAAAAAAAAAAAAAA, defaults, back home.
    [Arguments]    ${name}    ${label_downs}
    Wait For Screen    ${name}    menu
    Presses    ${name}    joyDown    3
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    text
    Presses    ${name}    joyDown    ${label_downs}
    Press    ${name}    joyRight
    Press    ${name}    joyCenter
    Presses    ${name}    joyRight    16
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    code_opts
    Press    ${name}    joyCenter
    Wait For Screen    ${name}    code
    Press    ${name}    joyLeft
    Press    ${name}    joyLeft
    Wait For Screen    ${name}    menu

Device Healthy
    [Arguments]    ${name}
    ${dbg}=    Debug Block    ${name}
    Should Be Equal As Integers    ${dbg}[flash_errors]    0
    Should Be Equal As Integers    ${dbg}[i2c_errors]    0
    Should Be Equal As Integers    ${dbg}[epd_timeouts]    0

*** Test Cases ***
Measures A Pairing And A Typical Use
    Create Device    alice    1
    Create Device    bob    2
    Execute Command    emulation CreateUARTHub "ir"
    Execute Command    mach set "alice"
    Execute Command    connector Connect sysbus.usart1 ir
    Execute Command    mach set "bob"
    Execute Command    connector Connect sysbus.usart1 ir
    FOR    ${dev}    IN    alice    bob
        Wait For Screen    ${dev}    menu    timeout_s=8
        Wait Until Display Idle    ${dev}
        # Left on the home menu: sleep at once. Sessions start asleep.
        Press    ${dev}    joyLeft
        Wait For Sleep    ${dev}    1
    END

    # ---- pairing: wake, Pair with someone, compare, name, idle to sleep ----
    Power    alice    Start "pairing"
    Power    bob    Start "pairing"
    FOR    ${dev}    IN    alice    bob
        Tap    ${dev}    joyCenter    menu
        Tap    ${dev}    joyDown
        Tap    ${dev}    joyDown
        Tap    ${dev}    joyCenter    pair
        Power    ${dev}    Mark "pair"
    END
    Wait For Screen    alice    sas    timeout_s=30
    Wait For Screen    bob    sas    timeout_s=30
    Power    alice    Mark "sas"
    Power    bob    Mark "sas"
    Wait Until Display Idle    alice
    Wait Until Display Idle    bob
    Run For    3    # the two people compare the six symbols
    FOR    ${dev}    IN    alice    bob
        Tap    ${dev}    joyCenter    text
        Tap    ${dev}    joyRight
        Tap    ${dev}    joyCenter    contact
        Power    ${dev}    Mark "named"
    END
    Wait For Sleep    alice    2
    Wait For Sleep    bob    2
    Power    alice    Save @${OUT}/power-pairing.json
    Power    bob    Save @${OUT}/power-pairing-peer.json

    # ---- set-up, not measured: three codes on alice ----
    Press    alice    joyCenter
    Add Code    alice    0
    Add Code    alice    1
    Add Code    alice    2
    Press    alice    joyLeft
    Wait For Sleep    alice    3

    # ---- a typical use ----
    Power    alice    Start "use"
    Tap    alice    joyCenter    menu
    Power    alice    Mark "wake"
    Run For    1
    Tap    alice    joyDown
    Tap    alice    joyCenter    contacts
    Tap    alice    joyCenter    contact
    Power    alice    Mark "contact words"
    Run For    10    # say the word, hear the answer
    Tap    alice    joyLeft    contacts
    Tap    alice    joyLeft    menu
    # The home menu opens on its first row, Codes.
    Tap    alice    joyCenter    codes
    Power    alice    Mark "codes"
    Run For    1
    Tap    alice    joyDown
    Tap    alice    joyDown
    Tap    alice    joyCenter    code
    Power    alice    Mark "code"
    Run For    6    # type the code into a website
    Tap    alice    joyUp
    Run For    2
    Tap    alice    joyUp
    Run For    2
    Power    alice    Mark "idle"
    Wait For Sleep    alice    4
    Power    alice    Save @${OUT}/power-use.json
    Device Healthy    alice
    Device Healthy    bob
