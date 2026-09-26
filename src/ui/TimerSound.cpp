#include "TimerSound.h"

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSoundEffect>
#include <QStandardPaths>
#include <QUrl>
#include <QtMath>

namespace {

constexpr int SampleRate = 44100;

struct Tone {
    double startSeconds;
    double frequency;
    double durationSeconds;
    double decay; // exponential decay rate (1/s)
};

// Soft chime, school-style bell, low gong.
QList<Tone> tonesFor(int index)
{
    switch (index) {
    case 1:
        return {{0.0, 880, 0.9, 5}, {0.0, 1320, 0.9, 7}, {0.35, 880, 0.9, 5}, {0.35, 1320, 0.9, 7},
                {0.7, 880, 1.2, 4}, {0.7, 1320, 1.2, 6}};
    case 2:
        return {{0.0, 110, 3.0, 1.1}, {0.0, 220, 3.0, 1.6}, {0.0, 331, 2.5, 2.2}, {0.0, 523, 1.5, 3.5}};
    default:
        return {{0.0, 660, 1.4, 3}, {0.0, 990, 1.2, 4}, {0.6, 784, 1.6, 2.6}, {0.6, 1176, 1.4, 3.6}};
    }
}

QByteArray synthesizeWav(int index)
{
    const QList<Tone> tones = tonesFor(index);
    double total = 0;
    for (const Tone &tone : tones)
        total = qMax(total, tone.startSeconds + tone.durationSeconds);

    const int sampleCount = int(total * SampleRate);
    QVector<double> mix(sampleCount, 0.0);
    for (const Tone &tone : tones) {
        const int start = int(tone.startSeconds * SampleRate);
        const int length = int(tone.durationSeconds * SampleRate);
        for (int i = 0; i < length && start + i < sampleCount; ++i) {
            const double t = double(i) / SampleRate;
            const double attack = qMin(1.0, t / 0.01);
            mix[start + i] += qSin(2 * M_PI * tone.frequency * t) * qExp(-tone.decay * t) * attack;
        }
    }
    double peak = 0.0001;
    for (double sample : std::as_const(mix))
        peak = qMax(peak, qAbs(sample));

    QByteArray pcm;
    pcm.reserve(sampleCount * 2);
    QDataStream pcmStream(&pcm, QIODevice::WriteOnly);
    pcmStream.setByteOrder(QDataStream::LittleEndian);
    for (double sample : std::as_const(mix))
        pcmStream << qint16(sample / peak * 0.7 * 32767);

    QByteArray wav;
    QDataStream out(&wav, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.writeRawData("RIFF", 4);
    out << quint32(36 + pcm.size());
    out.writeRawData("WAVEfmt ", 8);
    out << quint32(16) << quint16(1) << quint16(1) << quint32(SampleRate) << quint32(SampleRate * 2)
        << quint16(2) << quint16(16);
    out.writeRawData("data", 4);
    out << quint32(pcm.size());
    wav.append(pcm);
    return wav;
}

} // namespace

namespace TimerSound {

QStringList names()
{
    return {QCoreApplication::translate("TimerSound", "Мягкий звук"),
            QCoreApplication::translate("TimerSound", "Звонок"),
            QCoreApplication::translate("TimerSound", "Гонг")};
}

void play(int index)
{
    static QHash<int, QSoundEffect *> effects;
    QSoundEffect *effect = effects.value(index);
    if (!effect) {
        const QString path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("sermon-timer-sound-%1.wav").arg(index));
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return;
        file.write(synthesizeWav(index));
        file.close();

        effect = new QSoundEffect(qApp);
        effect->setSource(QUrl::fromLocalFile(path));
        effect->setVolume(0.9);
        effects.insert(index, effect);
    }
    effect->stop();
    effect->play();
}

} // namespace TimerSound
