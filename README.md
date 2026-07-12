# IRL2FA - Air-Gapped Real-World 2-Factor Authenticator

IRL2FA is a compact physical device designed to verify the authenticity of digital communications in an increasingly AI-dominated world. Its primary purpose is to prevent AI impersonation fraud in personal communications, while also serving as a standalone 2FA (Two-Factor Authentication) device for those who prefer not to have continuous access to their phones.

## Theory of Operation

IRL2FA represents a practical solution to the growing challenge of AI impersonation, providing a physical anchor of trust in our increasingly digital world. While no security system is perfect, this air-gapped approach offers a strong foundation for verified personal communications.

### Contact Pairing Process

When two individuals want to establish a trusted connection, they follow this secure pairing process:

1. Both parties should be physically present with their IRL2FA devices
2. Each person activates pairing mode on their device
3. One person's device generates a random 20-digit sequence
 - Uses arrows and enter keys for input
 - Provides 95 trillion possible combinations
 - Ensures statistical uniqueness of each pairing while providing pairing ease in comparison to alphanumeric strings
4. The other person enters the exact sequence on their device
5. Each person enters a name to identify the new contact
6. The devices establish a synchronized seed for generating verification words

### Authentication Process

Once paired, the devices generate synchronized pairs of verification words every 15 seconds:

Each pair consists of two words:
 - A "Give" word: The word you provide to prove your identity
 - A "Receive" word: The word you expect from the other person

Example authentication flow:

1. Your device displays: "Give: Mountain, Receive: Ocean"
2. When starting a conversation, you send "Mountain"
3. The other person must respond with "Ocean"

If they provide the correct word, you've verified their physical possession of the paired device

### Standard Authenticator Codes
The IRL2FA device doubles as a standard two-factor authentication (MFA) token generator:

- Supports up to 512 separate authentication codes
- Compatible with standard TOTP (Time-based One-Time Password) protocol
- Manual entry of authentication secrets instead of QR code scanning requires entering the base32-encoded secret key directly
 - Generates industry-standard 6-digit codes every 30 seconds
 - Works with any service that supports standard TOTP tokens
 - Each code can be labeled for easy identification

### Security Considerations

The IRL2FA system is built on several security principles:

 - Physical presence for initial pairing
 - Air-gapped operation prevents remote attacks
 - No digital connections eliminate common attack vectors
 - Time-based word generation prevents replay attacks
 - Large key space (95 trillion combinations) prevents brute force
 - 15-second rotation interval limits attack windows

