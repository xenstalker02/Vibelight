# Troubleshooting

## 1. Mic not transmitting
**Symptom:** Remote PC does not hear your mic.
**Fix:** Add `micCapture=true` to Moonlight.conf under `[General]`.

## 2. Wrong mic device selected
**Symptom:** Wrong microphone is captured, or no audio despite micCapture=true.
**Fix:** Add `micDevice=<device name>` to Moonlight.conf. Find device names with `arecord -l` on the Deck.

## 3. Opus encode errors in log
**Symptom:** Log shows `opus_encode error: invalid argument` or similar.
**Fix:** Usually caused by a sample rate mismatch. Check that micBitrate is in the valid range (6000--510000). Default 64000 is the correct value. If you have an old Moonlight.conf with micBitrate=96000, change it to 64000.

## 4. Control stream not connecting
**Symptom:** "Control stream establishment failed: error 11" or similar.
**Fix:** On the Windows PC, add an inbound Windows Firewall rule: UDP port 47999, allow. See Vibepollo TROUBLESHOOTING.md.

## 5. Flatpak mic permission denied
**Symptom:** SDL_OpenAudioDevice fails with permission error.
**Fix:** Run: `flatpak override --user --device=all com.moonlight_stream.Moonlight`
Then relaunch Vibelight.

## 6. install.sh fails
**Symptom:** Build error during `bash install.sh`.
**Fix:** Ensure flatpak-builder is installed: `flatpak install --user flathub org.flatpak.Builder`
Then retry `bash install.sh`.

## 7. Mic audio choppy
**Symptom:** Mic transmits but audio is choppy or drops.
**Fix:** Reduce `micBitrate=` in Moonlight.conf. Try 48000 or 32000.

## 8. HOME/AWAY label not appearing
**Symptom:** Server shows as "hostname" without (HOME) or (AWAY) suffix.
**Fix:** You must launch Vibelight via the `moonlight_wake.sh run` script, not directly from the app icon. The wake script sets the label before Moonlight connects.

## 9. customname being reset
**Symptom:** HOME/AWAY label disappears after connecting.
**Fix:** The wake script sets `1\\customname=true` in Moonlight.conf to prevent the server from overwriting your custom label. If this stops working, re-run the wake script in test mode to re-apply.
