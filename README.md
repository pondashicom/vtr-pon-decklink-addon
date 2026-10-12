# VTR-PON DeckLink Video and Audio I/O Add-on

Version **0.3.0** supports **VTR-PON 3.0.0-beta.1** on Windows 11 x64. Both VTR-PON and the add-on must use x64 binaries. This add-on is distributed separately for the initial VTR-PON3 beta release.

Adds video and audio input, ON AIR output, and fill/key output through Blackmagic Design DeckLink devices.

## Installation, updates, and removal

1. Download `vtr-pon-decklink-addon-0.3.0-windows-x64.vtrponaddon` from the [official VTR-PON3 add-on page](https://pondashi.com/vtr-pon/addon/) and verify it against the published SHA-256 checksum.
2. Install the required software listed below. You do not need the SDK to use the add-on.
3. In VTR-PON, open **Settings → Add-ons → Install from package…** and select the file. Read the README, license terms, and notices before accepting the terms. You can cancel if you do not agree.
4. Follow VTR-PON's prompts to close and restart the application, then check the add-on's status.

To update, select the new version's package in the same settings page, review its documents, and accept its terms.

To remove the add-on, open **Settings → Add-ons** and follow the prompts to restart VTR-PON. Removing an add-on does not remove external runtimes, drivers, or plug-ins. Follow each vendor's instructions to update or remove those components.

## Required software and usage

Install the Windows x64 version of [Blackmagic Design Desktop Video](https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software) separately, including its drivers and API runtime. Complete any required Windows restart. API version 16.0 or later is required; Desktop Video 16.4.0.0 was used for this release. Configure devices, channels, input/output profiles, and connectors in Desktop Video Setup.

Configure input and output under **Settings → DeckLink** in VTR-PON. For input, select a device, channel, and connector, then add it to the playlist. Configure Composite, Fill, and Key outputs separately. Fill and key must use different physical channels with the same video format. Use a shared reference signal where the hardware supports it. Mixed audio is sent to the composite and fill outputs; the key output is silent.

## Compatibility and limitations

Available input/output formats, connectors, full-duplex operation, automatic format detection, and synchronized playback depend on the device and its profile. Specify the format manually if automatic detection is not supported. Physical synchronization is not guaranteed when fill and key are played independently.

The current source rejects automatic input mode on devices without format detection instead of silently choosing the first format. Select the actual source format before adding the input; changing this default does not change items already in the playlist. Half-duplex channels can capture or output, but cannot do both at once. Multiple connectors do not imply multiple independent channels. A busy channel is rejected before its configuration or streams are changed.

The unreleased input fixes were accepted with SDI 1080p59.94 and direct-to-card HDMI 1080p29.97 video and audible stereo on DeckLink HD Extreme 3D. The HDMI bracket path on this test unit was treated as an internal cable fault. The current fill/key implementation requires two independent logical channels; the single-channel hardware external-keyer path is not yet implemented. See the hardware check record for exact acceptance conditions and remaining work.

**Unreleased source update (2026-10-12):** Older cards without a persistent hardware ID, including DeckLink HD Extreme 3D, are identified by their PCIe location. Reselect their input/output channels in VTR-PON after moving or replacing the card. Ambiguous device identifiers are not offered for selection. Output startup now includes audio preroll and retains device configuration for the output session. HDMI 1080p60 video playback and SDI video output were checked with this card; see the [hardware check record and remaining checks](docs/hardware-checklist.md). These fixes are not included in the existing 0.3.0 package.

This add-on targets VTR-PON 3.0.0-beta.1. For other VTR-PON versions, check the compatibility information in the relevant release.

## License, source code, and support

The add-on's original code is licensed under the MIT License, with copyright held by Tetsu Suzuki. The project imposes no additional restrictions on commercial or non-commercial productions. External software and third-party code remain subject to their respective terms.

- [MIT License](https://github.com/pondashicom/vtr-pon-decklink-addon/blob/v0.3.0/LICENSE/MIT.txt) · [Distribution terms and third-party notices](https://github.com/pondashicom/vtr-pon-decklink-addon/blob/v0.3.0/LICENSE/DISTRIBUTION.md)
- [Source code for this version](https://github.com/pondashicom/vtr-pon-decklink-addon/tree/v0.3.0) · [Build instructions](https://github.com/pondashicom/vtr-pon-decklink-addon/blob/v0.3.0/BUILDING.md) · [Release](https://github.com/pondashicom/vtr-pon-decklink-addon/releases/tag/v0.3.0)
- [VTR-PON source code](https://github.com/pondashicom/vtr-pon/tree/main/3) · [VTR-PON website](https://pondashi.com/vtr-pon/)
- Report bugs through [Issues](https://github.com/pondashicom/vtr-pon-decklink-addon/issues). Include the add-on and VTR-PON versions, Windows version, relevant runtimes, hardware and plug-ins, and steps to reproduce the issue. For private inquiries, contact [info@pondashi.com](mailto:info@pondashi.com).

This add-on is not an official or certified product of any hardware, SDK, or plug-in vendor. All product names and trademarks belong to their respective owners.
