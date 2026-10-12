#include <QApplication>
#include <QCapturableWindow>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaCaptureSession>
#include <QMediaFormat>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMediaRecorder>
#include <QPainter>
#include <QSaveFile>
#include <QScreen>
#include <QSysInfo>
#include <QTimer>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>
#include <QWidget>
#include <QWindowCapture>
#include <functional>

class PatternWindow final : public QWidget {
public:
    int tick = 0;
    PatternWindow() {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        setFixedSize(640, 360);
        setWindowTitle(QStringLiteral("ScreenPilot capture probe PID %1")
                           .arg(QCoreApplication::applicationPid()));
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), Qt::black);
        p.fillRect(QRect(0, 0, 213, 240), QColor(255, 0, 0));
        p.fillRect(QRect(213, 0, 214, 240), QColor(0, 255, 0));
        p.fillRect(QRect(427, 0, 213, 240), QColor(0, 0, 255));
        p.fillRect(QRect(0, 240, 640, 120),
                   tick % 2 ? QColor(255, 255, 255) : QColor(255, 255, 0));
        p.setPen(Qt::black);
        p.setFont(QFont(QStringLiteral("Consolas"), 25));
        p.drawText(QRect(20, 260, 600, 80), QString::number(tick));
    }
};

struct Evidence {
    qint64 frames = 0, firstPts = -1, lastPts = -1;
    qint64 firstChangePts = -1, lastChangePts = -1;
    int nonMonotonic = 0, missingPts = 0, colors = 0, changes = 0;
    int conversionFailures = 0, wrongDimensions = 0;
    QSize size;
    QColor lastDynamic;
    bool saved = false;
    QElapsedTimer wall;

    void consume(const QVideoFrame &frame, const QString &png, bool decoded) {
        if (!frame.isValid()) return;
        if (!frames) wall.start();
        ++frames;
        const qint64 pts = frame.startTime(); // Qt timestamps are microseconds.
        if (pts < 0) ++missingPts;
        else {
            if (firstPts < 0) firstPts = pts;
            if (lastPts >= 0 && pts <= lastPts) ++nonMonotonic;
            lastPts = pts;
        }
        size = frame.size();
        if (decoded && size != QSize(640, 360)) ++wrongDimensions;
        const QImage image = frame.toImage();
        if (image.isNull()) { ++conversionFailures; return; }
        if (!saved) saved = image.save(png);
        // Sample the known interior patches; a changing black/noise hash cannot pass.
        auto sample = [&](double x, double y) {
            return image.pixelColor(qBound(0, int(image.width() * x), image.width()-1),
                                    qBound(0, int(image.height() * y), image.height()-1));
        };
        const QColor r = sample(.16, .30), g = sample(.50, .30), b = sample(.83, .30);
        if (r.red() > 160 && r.green() < 90 && r.blue() < 90
            && g.green() > 160 && g.red() < 90 && g.blue() < 90
            && b.blue() > 160 && b.red() < 90 && b.green() < 90) ++colors;
        const QColor dynamic = sample(.90, .90);
        if (lastDynamic.isValid() && qAbs(dynamic.blue() - lastDynamic.blue()) > 100) {
            ++changes;
            if (firstChangePts < 0) firstChangePts = pts;
            lastChangePts = pts;
        }
        lastDynamic = dynamic;
    }
    bool valid() const {
        return frames >= 10 && colors >= 5 && changes >= 2 && saved
            && !conversionFailures && !wrongDimensions && !missingPts
            && !nonMonotonic && lastPts > firstPts
            && lastChangePts - firstChangePts >= (lastPts - firstPts) * 0.6;
    }
    QJsonObject json() const {
        const double span = (lastPts - firstPts) / 1000000.0;
        return {{"frame_count", frames}, {"first_pts_us", firstPts}, {"last_pts_us", lastPts},
                {"non_monotonic_pts", nonMonotonic}, {"missing_pts", missingPts},
                {"color_pattern_frames", colors}, {"dynamic_color_changes", changes},
                {"first_change_pts_us", firstChangePts}, {"last_change_pts_us", lastChangePts},
                {"conversion_failures", conversionFailures}, {"wrong_dimensions", wrongDimensions},
                {"width", size.width()}, {"height", size.height()}, {"png_saved", saved},
                {"pts_span_seconds", span}, {"observed_fps", span > 0 ? (frames-1)/span : 0},
                {"wall_ms", wall.isValid() ? wall.elapsed() : 0}, {"passed", valid()}};
    }
};

int main(int argc, char **argv)
{
    // QWindowCapture requires Qt's FFmpeg backend. No audio input is attached.
    qputenv("QT_MEDIA_BACKEND", "ffmpeg");
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ScreenPilotWindowCaptureProbe"));
    QCommandLineParser cli;
    cli.addHelpOption();
    cli.addOption({"output-dir", "Empty directory for probe artifacts", "directory"});
    cli.addOption({"seconds", "Recording duration (3..30)", "seconds", "5"});
    cli.addOption({"invalid-window", "Negative test: search for a nonexistent title"});
    cli.process(app);
    bool secondsOk = false;
    const int seconds = cli.value("seconds").toInt(&secondsOk);
    const QString output = cli.isSet("output-dir") ? QDir(cli.value("output-dir")).absolutePath()
        : QDir::current().absoluteFilePath("capture-probe-" +
              QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz"));
    QDir dir(output);
    if (dir.exists() && !dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty()) {
        qCritical("Output directory is not empty; refusing to overwrite any file.");
        return 2;
    }
    if (!QDir().mkpath(output)) { qCritical("Cannot create output directory."); return 2; }
    QElapsedTimer total;
    total.start();
    PatternWindow window;
    QTimer animation;
    QObject::connect(&animation, &QTimer::timeout, &window, [&] { ++window.tick; window.update(); });
    animation.start(100);
    QMediaCaptureSession session;
    QWindowCapture capture;
    QVideoSink capturedSink, decodedSink;
    QMediaRecorder recorder;
    QMediaPlayer player;
    Evidence captured, decoded;
    bool done = false, recorded = false, stopRequested = false, playbackStarted = false;
    bool activeSeen = false, stoppedSeen = false;
    QString captureError, recorderError, playerError;
    QJsonArray recorderStates, activeStates, screens;
    for (QScreen *screen : app.screens())
        screens.append(QJsonObject{{"name", screen->name()}, {"width", screen->size().width()},
                                  {"height", screen->size().height()}, {"dpr", screen->devicePixelRatio()}});
    const QString movie = dir.filePath("capture.mp4");
    std::function<void(bool, const QString &)> finish;
    finish = [&](bool success, const QString &reason) {
        if (done) return;
        done = true;
        const QMediaMetaData metadata = player.metaData();
        player.stop();
        capture.stop();
        if (recorder.recorderState() != QMediaRecorder::StoppedState) recorder.stop();
        QJsonObject result{{"success", success}, {"reason", reason}, {"qt_version", qVersion()},
            {"os", QSysInfo::prettyProductName()}, {"screens", screens},
            {"window_title", window.windowTitle()}, {"window_dpr", window.devicePixelRatioF()},
            {"requested_seconds", seconds}, {"requested_fps", 30}, {"audio_input_attached", false},
            {"backend", "ffmpeg"}, {"capture", captured.json()}, {"decode", decoded.json()},
            {"decoded_video_codec", metadata.stringValue(QMediaMetaData::VideoCodec)},
            {"decoded_file_format", metadata.stringValue(QMediaMetaData::FileFormat)},
            {"decoded_metadata_fps", metadata.value(QMediaMetaData::VideoFrameRate).toDouble()},
            {"recorder_file_format_enum", int(recorder.mediaFormat().fileFormat())},
            {"recorder_video_codec_enum", int(recorder.mediaFormat().videoCodec())},
            {"capture_active_seen", activeSeen}, {"capture_active_events", activeStates},
            {"recorder_states", recorderStates}, {"recorder_stopped_seen", stoppedSeen},
            {"capture_error", captureError}, {"recorder_error", recorderError},
            {"player_error", playerError}, {"recorder_error_code", int(recorder.error())},
            {"recorder_duration_ms", recorder.duration()}, {"playback_duration_ms", player.duration()},
            {"file_bytes", QFileInfo(movie).size()}, {"total_wall_ms", total.elapsed()},
            {"actual_location", recorder.actualLocation().toLocalFile()}, {"output_dir", output}};
        QSaveFile file(dir.filePath("result.json"));
        const QByteArray json = QJsonDocument(result).toJson(QJsonDocument::Indented);
        const bool written = file.open(QIODevice::WriteOnly) && file.write(json) == json.size() && file.commit();
        qInfo().noquote() << QString::fromUtf8(json);
        app.exit(success && written ? 0 : 1);
    };
    QObject::connect(&capturedSink, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &f) {
        if (!done) captured.consume(f, dir.filePath("first-captured.png"), false);
    });
    QObject::connect(&decodedSink, &QVideoSink::videoFrameChanged, &app, [&](const QVideoFrame &f) {
        if (!done) decoded.consume(f, dir.filePath("first-decoded.png"), true);
    });
    QObject::connect(&capture, &QWindowCapture::activeChanged, &app, [&](bool active) {
        activeSeen |= active;
        activeStates.append(QJsonObject{{"active", active}, {"wall_ms", total.elapsed()}});
    });
    QObject::connect(&capture, &QWindowCapture::errorOccurred, &app,
        [&](QWindowCapture::Error e, const QString &s) {
            captureError = QString::number(int(e)) + ": " + s;
            finish(false, "window_capture_error");
        });
    QObject::connect(&recorder, &QMediaRecorder::errorOccurred, &app,
        [&](QMediaRecorder::Error, const QString &s) { recorderError = s; finish(false, "recorder_error"); });
    QObject::connect(&player, &QMediaPlayer::errorOccurred, &app,
        [&](QMediaPlayer::Error, const QString &s) { playerError = s; finish(false, "playback_error"); });
    QObject::connect(&player, &QMediaPlayer::mediaStatusChanged, &app, [&](QMediaPlayer::MediaStatus status) {
        if (status != QMediaPlayer::EndOfMedia || done) return;
        const qint64 duration = player.duration();
        const bool durationOk = duration >= seconds * 1000 - 750 && duration <= seconds * 1000 + 1000;
        const bool coverageOk = decoded.firstPts <= 250000
            && decoded.lastPts >= duration * 1000 - 250000
            && decoded.lastPts <= duration * 1000 + 250000;
        const bool formatOk = recorder.mediaFormat().fileFormat() == QMediaFormat::MPEG4
            && recorder.mediaFormat().videoCodec() == QMediaFormat::VideoCodec::H264
            && player.metaData().value(QMediaMetaData::VideoCodec).value<QMediaFormat::VideoCodec>()
                == QMediaFormat::VideoCodec::H264;
        const bool ok = captured.valid() && decoded.valid() && durationOk && coverageOk && formatOk && activeSeen && stoppedSeen
            && recorder.error() == QMediaRecorder::NoError && captureError.isEmpty()
            && recorderError.isEmpty() && playerError.isEmpty() && QFileInfo(movie).size() > 1024;
        finish(ok, ok ? "encoded_and_decoded_evidence_passed" : "evidence_validation_failed");
    });
    // StoppedState is the Qt contract for completion of pending encoding writes.
    // Additionally require a stable size and readable file before opening the decoder.
    QTimer flushPoll;
    qint64 previousSize = -1;
    int stablePolls = 0;
    QObject::connect(&flushPoll, &QTimer::timeout, &app, [&] {
        if (done || playbackStarted) { flushPoll.stop(); return; }
        const qint64 size = QFileInfo(movie).size();
        stablePolls = size > 1024 && size == previousSize ? stablePolls + 1 : 0;
        previousSize = size;
        if (stablePolls < 3) return;
        QFile readable(movie);
        if (!readable.open(QIODevice::ReadOnly)) return;
        readable.close();
        playbackStarted = true;
        flushPoll.stop();
        player.setVideoSink(&decodedSink);
        // Keep the longest permitted recording plus real playback below the global deadline.
        player.setPlaybackRate(2.0);
        player.setSource(QUrl::fromLocalFile(movie));
        player.play();
    });
    QObject::connect(&recorder, &QMediaRecorder::recorderStateChanged, &app,
        [&](QMediaRecorder::RecorderState state) {
            recorderStates.append(QJsonObject{{"state", int(state)}, {"wall_ms", total.elapsed()}});
            if (state == QMediaRecorder::RecordingState && !recorded) {
                recorded = true;
                QTimer::singleShot(seconds * 1000, &app, [&] {
                    if (done) return;
                    stopRequested = true;
                    recorder.stop();
                });
            }
            if (state == QMediaRecorder::StoppedState && recorded && stopRequested && !done) {
                stoppedSeen = true;
                capture.stop();
                flushPoll.start(150);
            }
        });
    QTimer::singleShot(58000, &app, [&] { finish(false, "total_timeout_58_seconds"); });
    window.show();
    QTimer::singleShot(800, &app, [&] {
        if (!secondsOk || seconds < 3 || seconds > 30) { finish(false, "seconds_must_be_3_to_30"); return; }
        if (cli.isSet("invalid-window")) {
            // No public native-ID constructor exists; never choose a different window.
            const QString absent = window.windowTitle() + " nonexistent negative target";
            for (const auto &candidate : QWindowCapture::capturableWindows()) {
                if (candidate.description() == absent) {
                    finish(false, "negative_target_unexpectedly_exists");
                    return;
                }
            }
            finish(false, "invalid_window_target_not_found_negative_test");
            return;
        }
        QMediaFormat format(QMediaFormat::MPEG4);
        format.setVideoCodec(QMediaFormat::VideoCodec::H264);
        format.setAudioCodec(QMediaFormat::AudioCodec::Unspecified);
        if (!format.isSupported(QMediaFormat::Encode) || !format.isSupported(QMediaFormat::Decode)) {
            finish(false, "h264_mpeg4_encode_or_decode_unsupported"); return;
        }
        QCapturableWindow target;
        int matches = 0;
        for (const auto &candidate : QWindowCapture::capturableWindows())
            if (candidate.description() == window.windowTitle() && candidate.isValid()) {
                target = candidate; ++matches;
            }
        if (matches != 1) { finish(false, "controlled_window_not_uniquely_found"); return; }
        session.setWindowCapture(&capture);
        session.setVideoSink(&capturedSink);
        session.setRecorder(&recorder);
        capture.setWindow(target);
        recorder.setMediaFormat(format);
        recorder.setVideoResolution(640, 360);
        recorder.setVideoFrameRate(30);
        recorder.setQuality(QMediaRecorder::HighQuality);
        recorder.setOutputLocation(QUrl::fromLocalFile(movie));
        capture.start();
        if (!done) recorder.record();
    });
    return app.exec();
}
