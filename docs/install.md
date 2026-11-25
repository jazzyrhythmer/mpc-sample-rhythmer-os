# Install RHYTHMER OS for MPC Sample

## Supported stock firmware

This build uses Melodee's installation support for **1.3.0.12, 1.4.0.7 and 1.4.0.8**. The launcher checks the stock application's identity before starting Rhythmer; an unknown firmware leaves the stock application running. Back up the card before replacing installation files.

## Firmware 1.4.0.7 or 1.4.0.8

1. In the stock firmware, open **SHIFT + PAD 8 (MIDI CONFIG)** and find **Dev Enhancements (Beta)**. Enable **Developer Mode**.
2. Power off. On a computer, extract the provided installation ZIP.
3. Copy **autorun.sh** and the **autorun** folder to the card's top level. Keep their names and folder structure. If the package includes a **Rhythmer** folder, merge that folder at the card's top level too; it contains the supplied plug-ins and content.
4. Safely eject the card, insert it into the MPC Sample and power on.
5. Accept the stock firmware's autorun confirmation. Wait for Rhythmer to finish starting before removing the card or changing its files.

On firmware 1.4, audio runs at **44.1 kHz**. **USB-camera Screen Mirror is unavailable** because that firmware does not allow the custom kernel module it needs. The stock firmware's Remote Screen feature is not a Rhythmer screen mirror. USB audio and the USB web interface are separate functions; see [Settings](settings.md).

## Firmware 1.3.0.12

Copy the package's **autorun** folder, and any supplied **Rhythmer** content folder, to the card's top level without changing the launcher or checksum files. Safely eject, insert the card and power on. This firmware uses the checked launcher inside autorun rather than the newer root-script startup flow.

## A card that already uses a boot manager

Keep the boot manager's existing launcher. Do not replace its autorun.sh with the standalone launcher above. Use an app package intended for that boot manager when one is supplied; a standalone SD package and a boot-manager app package are different layouts. The normal installer refuses to replace another top-level launcher.

## After startup

Press **SAMPLE** for the selected track's sound. Hold **PAD BANK** and press a pad to select another track. Press **PAD FX + KNOB FX** together for the Mixer. Hold the encoder for **MENU → HERE → TRACK SETUP** to change an instrument or track type.

Project files and recovery checkpoints have separate locations. A manual **SAVE / SAVE AS** uses the destination you choose; recovery updates one matching file per project under **Rhythmer/Autosave**. See [Projects and files](projects-and-files.md).

## Return to the stock application

Power off and boot without the Rhythmer card. Removing a Rhythmer installation should not remove your samples or saved projects. Keep the card backup until you have confirmed the files you need are present.

RHYTHMER OS is developed by Jazzy Rhythmer and based on Melodee by Kerem Kiliç (Ellic Studio), with the credited open-source engine authors.

The full Rhythmer SD package also contains `Rhythmer/Plugins`: merge its supplied instrument files into that folder on the card. Keep existing projects, samples and user sounds; do not delete the entire Rhythmer folder. App-only updates do not install the new engine payloads.
