# User interface

The 5-way joystick is the **only control**: ↑ ↓ ← → and a centre push (●).
The rules are the same on every screen:

| Input | Meaning |
|-|-|
| ↑ ↓ | move / change a value (hold to repeat) |
| → or ● | open / select |
| ← | back (on the home menu: sleep now) |
| ● | confirm / options |

The device sleeps after 30 s without input. Before it does, it draws a
neutral sleep screen, so no code or word stays visible on the e-paper. Any
press wakes it. If a PIN is set, it asks for the PIN first.

Every screen below is a real render from the firmware (`firmware/tools/screens.py`).

## Map

```mermaid
flowchart LR
    wake([press to wake]) --> pin{PIN set?}
    pin -- yes --> PIN[Enter PIN] --> home
    pin -- no --> home
    home[Home menu] --> codes[Codes list] --> code[Code view]
    code -- "●" --> codeopts[Rename / Delete]
    home --> contacts[Contacts list] --> contact[Words view]
    contact -- "●" --> contactopts[Rename / Delete]
    home --> pair[Pair: searching] --> sas[Compare symbols] -- "● match" --> name[Name contact] --> contact
    home --> add[Add code: name] --> secret[Secret, base32] --> opts[Digits / period / algorithm] --> code
    home --> settings[Settings]
    settings --> time[Date & time]
    settings --> tz[Time zone]
    settings --> setpin[Set / change / remove PIN]
    settings --> about[About]
    settings --> wipe[Erase everything]
```

## Screens

| | |
|-|-|
| ![Home](images/screens/home.png) **Home.** Counts of codes and contacts. | ![Codes](images/screens/codes.png) **Codes.** Live codes next to each label. |
| ![Code](images/screens/code.png) **Code.** Big digits and time left. ↑↓ jumps to the next code. | ![Contact](images/screens/contact.png) **Words.** SAY your word, HEAR theirs. "or …" is the previous window's word. |
| ![Pair](images/screens/pair.png) **Pairing.** Hold the top edges together. | ![SAS](images/screens/sas.png) **Compare.** Both screens must show the same six symbols. |
| ![Text](images/screens/text.png) **Text entry.** ↑↓ picks a character from the wheel, → adds it, ← deletes, ● finishes. | ![PIN](images/screens/pin.png) **PIN.** A sequence of 4 to 12 joystick directions. |
| ![Time](images/screens/time.png) **Date & time.** ←→ selects a field, ↑↓ changes it. | ![Sleep](images/screens/sleep.png) **Asleep.** Shows nothing sensitive. |

## Entering a TOTP secret

Services show a QR code and, usually behind a "can't scan it?" link, the
same secret as base32 text (for example `JBSWY3DPEHPK3PXP`). Enter that text:

1. Home → **Add a code** → type a label (up to 15 characters).
2. Type the secret. The wheel only offers base32 characters (A-Z, 2-7), and
   spaces in the service's display can be skipped. 16 to 32 characters is
   typical.
3. Options: 6 or 8 digits, 30 or 60 s, SHA1/SHA256/SHA512. Nearly every
   service uses the defaults (6, 30 s, SHA1), so just press ●.
4. Compare the first code with the one the service asks for to finish setup.

Tip: hold ↑ or ↓ to spin the wheel quickly. After a delete, the wheel starts
on the character you deleted, so fixing a typo takes one press.
