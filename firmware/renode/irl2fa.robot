*** Comments ***
Emulation tests for the IRL2FA firmware (STM32U083 port) in Renode.
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
${OUT}            ${CURDIR}/../build/renode
${UNIX}           1800000000    # 2027-01-15 08:00:00 UTC
@{DEVICES}

*** Keywords ***
Load Models
    Setup
    Execute Command    include @${CURDIR}/models/STM32U0_System.cs
    Execute Command    include @${CURDIR}/models/SSD1680.cs
    Execute Command    include @${CURDIR}/models/RV3032.cs
    Execute Command    include @${CURDIR}/models/PowerMonitor.cs
    Execute Command    emulation SetGlobalQuantum "0.0001"

Create Device
    [Arguments]    ${name}    ${seed}    ${time_valid}=True
    Execute Command    mach create "${name}"
    Execute Command    machine LoadPlatformDescription @${CURDIR}/irl2fa.repl
    Execute Command    $rngseed=${seed}
    Execute Command    $unix=${UNIX}
    Execute Command    include @${CURDIR}/board_init.resc
    IF    not ${time_valid}
        # A fresh RTC: power-on flag set, no "time valid" marker.
        Execute Command    sysbus.i2c1.rtc Reset
    END
    Execute Command    sysbus LoadELF @${ELF}
    Append To List    ${DEVICES}    ${name}

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

Screen Of
    [Arguments]    ${name}
    ${dbg}=    Debug Block    ${name}
    RETURN    ${dbg}[screen]

Wait For Screen
    [Arguments]    ${name}    ${expected}    ${timeout_s}=10
    ${steps}=    Evaluate    int(${timeout_s} * 10)
    FOR    ${i}    IN RANGE    ${steps}
        ${scr}=    Screen Of    ${name}
        IF    '${scr}' == '${expected}'    RETURN
        Run For    0.1
    END
    ${scr}=    Screen Of    ${name}
    Should Be Equal    ${scr}    ${expected}    ${name}: screen is ${scr} after ${timeout_s} s, expected ${expected}

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

Epd Int
    [Arguments]    ${name}    ${what}
    Execute Command    mach set "${name}"
    ${out}=    Execute Command    sysbus.spi1.epd ${what}
    ${v}=    Monitor Int    ${out}
    RETURN    ${v}

Epd Text
    [Arguments]    ${name}    ${what}
    Execute Command    mach set "${name}"
    ${out}=    Execute Command    sysbus.spi1.epd ${what}
    ${v}=    Monitor Text    ${out}
    RETURN    ${v}

Dump Screen
    [Arguments]    ${name}    ${file}
    Execute Command    mach set "${name}"
    Execute Command    sysbus.spi1.epd DumpPng @${OUT}/${file}.png 3
    Execute Command    sysbus.spi1.epd DumpPbm @${OUT}/${file}.pbm

Device Healthy
    [Arguments]    ${name}
    ${dbg}=    Debug Block    ${name}
    Should Be Equal As Integers    ${dbg}[magic]    0x47424449
    Should Be Equal As Integers    ${dbg}[flash_errors]    0
    Should Be Equal As Integers    ${dbg}[i2c_errors]    0
    Should Be Equal As Integers    ${dbg}[epd_timeouts]    0
    ${v}=    Epd Int    ${name}    BackPowerViolations
    Should Be Equal As Integers    ${v}    0    ${name}: a panel line was high while the panel was unpowered
    ${v}=    Epd Int    ${name}    PartialOldImageMismatches
    Should Be Equal As Integers    ${v}    0    ${name}: a partial update had the wrong old image in RAM 0x26
    ${v}=    Epd Int    ${name}    CommandsWhileBusy
    Should Be Equal As Integers    ${v}    0    ${name}: command sent while BUSY was high

Save Screens And Test Teardown
    ${test}=    Evaluate    re.sub(r'[^A-Za-z0-9]+', '-', $TEST_NAME).strip('-').lower()    modules=re
    FOR    ${name}    IN    @{DEVICES}
        Run Keyword And Ignore Error    Dump Screen    ${name}    last-${test}-${name}
    END
    ${empty}=    Create List
    Set Suite Variable    @{DEVICES}    @{empty}
    Test Teardown

*** Test Cases ***
Boots To The Home Menu With A Full Refresh
    Create Device    dev    1
    Wait For Screen    dev    menu    timeout_s=8
    Wait Until Display Idle    dev
    ${full}=    Epd Int    dev    FullUpdates
    Should Be True    ${full} >= 1    first frame must be a full refresh
    ${last}=    Epd Text    dev    LastUpdate
    Should Contain    ${last}    full
    # Title bar: inverted (mostly black) 250 x 15 strip.
    ${title}=    Epd Int    dev    BlackPixels 0 0 250 15
    Should Be True    ${title} > 2500    title bar not drawn (${title} black pixels)
    # First menu row "Codes (0)" is selected: an inverted 244 x 18 bar.
    ${sel}=    Epd Int    dev    BlackPixels 0 17 244 18
    Should Be True    ${sel} > 3000    selection bar not drawn (${sel})
    # The other rows hold text: some ink, far from inverted.
    ${rows}=    Epd Int    dev    BlackPixels 0 35 244 72
    Should Be True    50 < ${rows} < 3000    menu rows look wrong (${rows})
    Dump Screen    dev    home
    Device Healthy    dev

Shows The Clock Screen When The RTC Lost Power
    Create Device    dev    1    time_valid=False
    Wait For Screen    dev    time    timeout_s=8
    Wait Until Display Idle    dev
    Dump Screen    dev    time
    Device Healthy    dev
    Execute Command    mach set "dev"
    ${clk}=    Execute Command    sysbus.i2c1.rtc ClkoutEnabled
    Should Contain    ${clk}    False    CLKOUT must be disabled at init
    ${ee}=    Execute Command    sysbus.i2c1.rtc EepromUpdates
    ${ee}=    Monitor Int    ${ee}
    Should Be Equal As Integers    ${ee}    1    CLKOUT setting must be stored in the RTC EEPROM once

Navigates To Add A Code And Back
    Create Device    dev    1
    Wait For Screen    dev    menu    timeout_s=8
    Wait Until Display Idle    dev
    ${before}=    Epd Text    dev    RegionHash 0 17 250 90
    Press    dev    joyDown
    Press    dev    joyDown
    Press    dev    joyDown
    ${after}=    Epd Text    dev    RegionHash 0 17 250 90
    Should Not Be Equal    ${before}    ${after}    selection did not move
    ${partial}=    Epd Int    dev    PartialUpdates
    Should Be True    ${partial} >= 3    menu moves should use fast (partial) refreshes
    # Row 4 ("Add a code") is now the inverted one.
    ${sel}=    Epd Int    dev    BlackPixels 0 71 244 18
    Should Be True    ${sel} > 3000    selection bar not on "Add a code" (${sel})
    Dump Screen    dev    add-code-selected
    Press    dev    joyCenter
    Wait For Screen    dev    text
    Dump Screen    dev    add-code-name
    # Left on an empty name cancels back to the home menu.
    Press    dev    joyLeft
    Wait For Screen    dev    menu
    ${home}=    Epd Text    dev    RegionHash 0 17 250 90
    Should Be Equal    ${home}    ${before}    home menu not restored (selection resets to the first row)
    Device Healthy    dev

Sleeps In Stop 2 And Wakes On The Joystick
    Create Device    dev    1
    Wait For Screen    dev    menu    timeout_s=8
    Wait Until Display Idle    dev
    # Left on the home menu sends the device to sleep at once.
    Press    dev    joyLeft
    Run For    5
    ${dbg}=    Debug Block    dev
    Should Be Equal As Integers    ${dbg}[sleeps]    1    device did not go to sleep
    ${polls}=    Set Variable    ${dbg}[polls]
    Dump Screen    dev    asleep
    Run For    5
    ${dbg}=    Debug Block    dev
    Should Be Equal As Integers    ${dbg}[polls]    ${polls}    the CPU ran app_poll while asleep
    Press    dev    joyCenter
    Wait For Screen    dev    menu
    ${dbg}=    Debug Block    dev
    Should Be True    ${dbg}[polls] > ${polls}    no wake-up
    Device Healthy    dev

Two Devices Pair Over IR
    Create Device    alice    1
    Create Device    bob    2
    Execute Command    emulation CreateUARTHub "ir"
    Execute Command    mach set "alice"
    Execute Command    connector Connect sysbus.usart1 ir
    Execute Command    mach set "bob"
    Execute Command    connector Connect sysbus.usart1 ir
    Wait For Screen    alice    menu    timeout_s=8
    Wait For Screen    bob    menu    timeout_s=8
    Wait Until Display Idle    alice
    Wait Until Display Idle    bob
    FOR    ${dev}    IN    alice    bob
        Press    ${dev}    joyDown
        Press    ${dev}    joyDown
        Press    ${dev}    joyCenter
        Wait For Screen    ${dev}    pair
    END
    Wait For Screen    alice    sas    timeout_s=20
    Wait For Screen    bob    sas    timeout_s=20
    Wait Until Display Idle    alice
    Wait Until Display Idle    bob
    Dump Screen    alice    sas-alice
    Dump Screen    bob    sas-bob
    # The six SAS symbols (y 40..72) must be identical on both screens.
    ${a}=    Epd Text    alice    RegionHash 0 38 250 36
    ${b}=    Epd Text    bob    RegionHash 0 38 250 36
    Should Be Equal    ${a}    ${b}    the two devices show different SAS symbols
    ${ink}=    Epd Int    alice    BlackPixels 0 38 250 36
    Should Be True    ${ink} > 500    no SAS symbols drawn (${ink})
    # Both people confirm: each device asks for a name for the new contact.
    Press    alice    joyCenter
    Press    bob    joyCenter
    Wait For Screen    alice    text
    Wait For Screen    bob    text
    Dump Screen    alice    name-contact
    Device Healthy    alice
    Device Healthy    bob
