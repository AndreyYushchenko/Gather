#pragma once

#include <QStringList>

// The "Сигнал в конце" chimes for the countdown. Synthesized once into small
// WAV files in the temp directory (no audio assets to ship) and played via
// QSoundEffect.
namespace TimerSound {

QStringList names();
void play(int index);

} // namespace TimerSound
