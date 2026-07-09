#include "GstRtspReceiver.h"

#include <gst/rtsp/gstrtsptransport.h>
#include <gst/video/videooverlay.h>

#include <QDebug>
#include <QMetaObject>
#include <QThread>
#include <QUrl>
#include <algorithm>
#include <cstring>

namespace {
constexpr int busPollIntervalMsec = 200;
constexpr int rtspLatencyMsec = 2000;
constexpr int initialPacketTimeoutMsec = 5000;
constexpr int initialFrameTimeoutMsec = 5000;
constexpr int maxReconnectDelayMsec = 30000;
constexpr int authenticationFailureReconnectDelayMsec = 120000;
constexpr int stallTimeoutMsec = 10000; // 연결끊김 시 재연결 시간
constexpr guint udpBufferSizeBytes = 1024 * 1024;
constexpr qint64 minimumLoadingMsec = 700;

bool hasGstFactory(const char* factoryName) {
    GstElementFactory* factory = gst_element_factory_find(factoryName);

    if (!factory) {
        return false;
    }

    gst_object_unref(factory);
    return true;
}

bool isH264VideoCaps(GstCaps* caps) {
    if (!caps || gst_caps_is_empty(caps)) {
        return false;
    }

    const GstStructure* structure = gst_caps_get_structure(caps, 0);

    if (!structure) {
        return false;
    }

    const char* media = gst_structure_get_string(structure, "media");
    const char* encodingName = gst_structure_get_string(structure, "encoding-name");

    return media && encodingName && std::strcmp(media, "video") == 0 && std::strcmp(encodingName, "H264") == 0;
}

bool isH264VideoPad(GstPad* pad) {
    if (!pad) {
        return false;
    }

    GstCaps* caps = gst_pad_get_current_caps(pad);

    if (!caps) {
        caps = gst_pad_query_caps(pad, nullptr);
    }

    const bool result = isH264VideoCaps(caps);

    if (caps) {
        gst_caps_unref(caps);
    }

    return result;
}

void setOptionalBooleanProperty(GstElement* element, const char* propertyName, gboolean value) {
    if (!element || !propertyName) {
        return;
    }

    GParamSpec* spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), propertyName);

    if (!spec) {
        return;
    }

    g_object_set(element, propertyName, value, nullptr);
}

bool hasCredentialPlaceholder(const QString& url) {
    return url.contains(QStringLiteral(":PASSWORD@"), Qt::CaseInsensitive);
}

QString normalizedGstErrorText(GError* error, const gchar* debugInfo) {
    const QString message = error ? QString::fromUtf8(error->message) : QStringLiteral("Unknown GStreamer error");
    const QString debugText = debugInfo ? QString::fromUtf8(debugInfo) : QString();

    if (debugText.contains(QStringLiteral("Account Blocked"), Qt::CaseInsensitive)) {
        return QStringLiteral("RTSP account blocked (490): check password or wait for NVR account unlock");
    }

    if (debugText.contains(QStringLiteral("Unauthorized"), Qt::CaseInsensitive) ||
        debugText.contains(QStringLiteral("(401)"), Qt::CaseInsensitive) ||
        message.contains(QStringLiteral("Unauthorized"), Qt::CaseInsensitive)) {
        return QStringLiteral("RTSP authentication failed: check user name/password");
    }

    if (message.compare(QStringLiteral("Unhandled error"), Qt::CaseInsensitive) == 0 && !debugText.isEmpty()) {
        return QStringLiteral("RTSP error: %1").arg(debugText.section(QLatin1Char('\n'), -1).trimmed());
    }

    return message;
}

bool isAuthenticationFailure(const QString& errorText) {
    return errorText.contains(QStringLiteral("account blocked"), Qt::CaseInsensitive) ||
           errorText.contains(QStringLiteral("authentication failed"), Qt::CaseInsensitive);
}
}  // namespace

GstRtspReceiver::GstRtspReceiver(guintptr outputWindowHandle, QObject* parent)
    : QObject(parent), outputWindowHandle_(outputWindowHandle) {
    busTimer_.setTimerType(Qt::PreciseTimer);
    reconnectTimer_.setSingleShot(true);

    connect(&busTimer_, &QTimer::timeout, this, &GstRtspReceiver::pollBus);

    connect(&reconnectTimer_, &QTimer::timeout, this, &GstRtspReceiver::startPipeline);
}

GstRtspReceiver::~GstRtspReceiver() { stop(); }

void GstRtspReceiver::setUrl(const QString& url) { url_ = url.trimmed(); }

void GstRtspReceiver::moveInternalObjectsToThread(QThread* thread) {
    if (!thread) {
        return;
    }

    busTimer_.moveToThread(thread);
    reconnectTimer_.moveToThread(thread);
    moveToThread(thread);
}

void GstRtspReceiver::start() {
    manualStop_ = false;
    reconnectAttempts_ = 0;

    reconnectTimer_.stop();

    startPipeline();
}

void GstRtspReceiver::startPipeline() {
    if (manualStop_) {
        return;
    }

    if (outputWindowHandle_ == 0) {
        emit errorOccurred("Output window handle is invalid");
        return;
    }

    if (url_.isEmpty()) {
        emit errorOccurred("RTSP URL is empty");
        return;
    }

    if (hasCredentialPlaceholder(url_)) {
        emit loadingChanged(false);
        emit errorOccurred(QStringLiteral("RTSP password placeholder is still set in network/rtsp.h"));
        emit statusChanged(QStringLiteral("RTSP configuration error"));
        return;
    }

    teardownPipeline();

    firstAsyncDoneReported_ = false;
    firstFrameReported_ = false;
    videoPadLinked_ = false;
    teardownInProgress_ = false;
    gotAnyPacket_.store(false, std::memory_order_relaxed);
    gotAnyFrame_.store(false, std::memory_order_relaxed);
    const gint64 startTimeUsec = g_get_monotonic_time();
    lastPacketTimeUsec_.store(startTimeUsec, std::memory_order_relaxed);
    lastFrameTimeUsec_.store(startTimeUsec, std::memory_order_relaxed);
    startupTimer_.restart();
    emit loadingChanged(true);

    windowHandle_ = outputWindowHandle_;

    if (!windowHandle_) {
        emit errorOccurred("Invalid video window handle");
        scheduleReconnect("invalid video window handle");
        return;
    }

    const QString videoChainDesc =
        QString(
            "rtph264depay name=depay request-keyframe=true wait-for-keyframe=true ! "
            "h264parse config-interval=-1 ! "
            "queue name=decodequeue max-size-buffers=90 max-size-bytes=0 max-size-time=3000000000 ! "
            "%1 ! "
            "queue name=renderqueue leaky=downstream max-size-buffers=4 max-size-bytes=0 max-size-time=0 ! "
            "identity name=framewatch silent=true ! "
            "d3d11videosink name=videosink force-aspect-ratio=true enable-last-sample=false sync=false async=false")
            .arg(decoderChain());

    qDebug().noquote() << "[GstRtspReceiver] Manual RTSP pipeline:" << videoChainDesc;

    GError* error = nullptr;
    GstElement* source = gst_element_factory_make("rtspsrc", "src");
    GstElement* videoChain = gst_parse_bin_from_description(videoChainDesc.toUtf8().constData(), TRUE, &error);

    if (!source || !videoChain) {
        const QString msg = error ? QString::fromUtf8(error->message) : "Failed to create RTSP video chain";

        emit errorOccurred(msg);

        if (error) {
            g_error_free(error);
        }

        if (source) {
            gst_object_unref(source);
        }

        if (videoChain) {
            gst_object_unref(videoChain);
        }

        scheduleReconnect(msg);
        return;
    }

    gst_element_set_name(videoChain, "videochain");

    if (error) {
        qDebug().noquote() << "[GStreamer Parse Warning]" << QString::fromUtf8(error->message);
        g_error_free(error);
    }

    pipeline_ = gst_pipeline_new(nullptr);

    if (!pipeline_) {
        emit errorOccurred("Failed to create pipeline");
        gst_object_unref(source);
        gst_object_unref(videoChain);
        scheduleReconnect("pipeline creation failed");
        return;
    }

    if (!applySourceProperties(source)) {
        gst_object_unref(source);
        gst_object_unref(videoChain);
        teardownPipeline();
        scheduleReconnect("invalid RTSP URL");
        return;
    }

    g_object_set(source, "latency", rtspLatencyMsec, "drop-on-latency", FALSE, "tcp-timeout",
                 static_cast<guint64>(20000000), "timeout", static_cast<guint64>(5000000), "probation", 1,
                 "udp-buffer-size", udpBufferSizeBytes, nullptr);
    setOptionalBooleanProperty(source, "do-rtsp-keep-alive", TRUE);
    setOptionalBooleanProperty(source, "udp-reconnect", TRUE);

    gst_bin_add_many(GST_BIN(pipeline_), source, videoChain, nullptr);

    g_signal_connect(source, "select-stream", G_CALLBACK(&GstRtspReceiver::onSelectStream), this);
    g_signal_connect(source, "pad-added", G_CALLBACK(&GstRtspReceiver::onPadAdded), this);
    g_signal_connect(source, "before-send", G_CALLBACK(&GstRtspReceiver::onBeforeSend), this);

    qDebug().noquote() << "[GstRtspReceiver] rtspsrc latency:" << rtspLatencyMsec
                       << "drop-on-latency:false protocols:defaults";

    if (GstBus* bus = gst_element_get_bus(pipeline_)) {
        gst_bus_set_sync_handler(bus, &GstRtspReceiver::onBusSyncMessage, this, nullptr);
        gst_object_unref(bus);
    }

    GstElement* videoChainForProbe = gst_bin_get_by_name(GST_BIN(pipeline_), "videochain");
    GstElement* depay = nullptr;
    GstElement* framewatch = nullptr;

    if (videoChainForProbe && GST_IS_BIN(videoChainForProbe)) {
        depay = gst_bin_get_by_name(GST_BIN(videoChainForProbe), "depay");
        framewatch = gst_bin_get_by_name(GST_BIN(videoChainForProbe), "framewatch");
    }

    if (depay) {
        if (GstPad* depaySinkPad = gst_element_get_static_pad(depay, "sink")) {
            gst_pad_add_probe(depaySinkPad, GST_PAD_PROBE_TYPE_BUFFER, &GstRtspReceiver::onPacketProbe, this, nullptr);
            gst_object_unref(depaySinkPad);
        }

        gst_object_unref(depay);
    } else {
        qWarning() << "[GstRtspReceiver] Failed to find depay";
    }

    if (framewatch) {
        if (GstPad* framewatchSrcPad = gst_element_get_static_pad(framewatch, "src")) {
            gst_pad_add_probe(framewatchSrcPad, GST_PAD_PROBE_TYPE_BUFFER, &GstRtspReceiver::onFrameProbe, this,
                              nullptr);
            gst_object_unref(framewatchSrcPad);
        }

        gst_object_unref(framewatch);
    } else {
        qWarning() << "[GstRtspReceiver] Failed to find framewatch";
    }

    if (videoChainForProbe) {
        gst_object_unref(videoChainForProbe);
    }

    GstElement* sink = nullptr;
    GstElement* videoChainForSink = gst_bin_get_by_name(GST_BIN(pipeline_), "videochain");

    if (videoChainForSink && GST_IS_BIN(videoChainForSink)) {
        sink = gst_bin_get_by_name(GST_BIN(videoChainForSink), "videosink");
    }

    if (videoChainForSink) {
        gst_object_unref(videoChainForSink);
    }

    if (!sink) {
        emit errorOccurred("Failed to find videosink");
        teardownPipeline();
        scheduleReconnect("videosink not found");
        return;
    }

    if (!GST_IS_VIDEO_OVERLAY(sink)) {
        emit errorOccurred("videosink does not support GstVideoOverlay");
        gst_object_unref(sink);
        teardownPipeline();
        scheduleReconnect("videosink overlay unsupported");
        return;
    }

    gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(sink), windowHandle_);
    gst_object_unref(sink);

    const GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);

    if (ret == GST_STATE_CHANGE_FAILURE) {
        emit errorOccurred("Failed to set pipeline to PLAYING");
        teardownPipeline();
        scheduleReconnect("state change failure");
        return;
    }

    busTimer_.start(busPollIntervalMsec);

    emit statusChanged("Connecting");
}

void GstRtspReceiver::stop() {
    manualStop_ = true;

    reconnectTimer_.stop();

    teardownPipeline();
    emit loadingChanged(false);
}

void GstRtspReceiver::teardownPipeline() {
    busTimer_.stop();

    if (pipeline_) {
        GstElement* pipeline = pipeline_;
        pipeline_ = nullptr;

        if (GstBus* bus = gst_element_get_bus(pipeline)) {
            gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
            gst_object_unref(bus);
        }

        teardownInProgress_ = true;
        gst_element_set_state(pipeline, GST_STATE_NULL);

        const GstStateChangeReturn ret = gst_element_get_state(pipeline, nullptr, nullptr, 5 * GST_SECOND);
        teardownInProgress_ = false;

        if (ret == GST_STATE_CHANGE_FAILURE) {
            qWarning() << "[GstRtspReceiver] Failed to set pipeline to NULL";
        } else if (ret == GST_STATE_CHANGE_ASYNC) {
            qWarning() << "[GstRtspReceiver] Timed out while waiting for pipeline NULL state";
        }

        gst_object_unref(pipeline);
    }

    windowHandle_ = 0;
}

void GstRtspReceiver::scheduleReconnect(const QString& reason, int overrideDelayMsec) {
    if (manualStop_ || reconnectTimer_.isActive()) {
        return;
    }

    const int backoffStep = std::min(reconnectAttempts_, 4);
    const int delayMsec =
        overrideDelayMsec > 0 ? overrideDelayMsec : std::min(maxReconnectDelayMsec, 1000 << backoffStep);
    ++reconnectAttempts_;

    emit loadingChanged(true);

    emit statusChanged(
        QString("Reconnect in %1 ms (attempt %2): %3").arg(delayMsec).arg(reconnectAttempts_).arg(reason));

    reconnectTimer_.start(delayMsec);
}

void GstRtspReceiver::restartPipeline(const QString& reason) {
    qDebug().noquote() << "[GstRtspReceiver] Restart:" << reason;
    emit statusChanged(reason);

    teardownPipeline();
    scheduleReconnect(reason);
}

bool GstRtspReceiver::applySourceProperties(GstElement* source) {
    if (!source) {
        return false;
    }

    QUrl rtspUrl(url_);
    QString location = url_;
    QString userName;
    QString password;

    if (rtspUrl.isValid() && !rtspUrl.scheme().isEmpty()) {
        userName = rtspUrl.userName(QUrl::FullyDecoded);
        password = rtspUrl.password(QUrl::FullyDecoded);
        location = rtspUrl.toString(QUrl::RemoveUserInfo);
    }

    if (location.trimmed().isEmpty()) {
        emit errorOccurred("RTSP location is empty");
        return false;
    }

    const QByteArray locationBytes = location.toUtf8();
    g_object_set(source, "location", locationBytes.constData(), nullptr);

    if (!userName.isEmpty()) {
        const QByteArray userBytes = userName.toUtf8();
        g_object_set(source, "user-id", userBytes.constData(), nullptr);
    }

    if (!password.isEmpty()) {
        const QByteArray passwordBytes = password.toUtf8();
        g_object_set(source, "user-pw", passwordBytes.constData(), nullptr);
    }

    return true;
}

void GstRtspReceiver::markFirstPacket() {
    if (manualStop_ || !pipeline_) {
        return;
    }

    const qint64 elapsed = startupTimer_.isValid() ? startupTimer_.elapsed() : 0;

    emit statusChanged(QString("First RTP/H264 packet in %1 ms").arg(elapsed));
    emit streamDataReceived();
}

void GstRtspReceiver::markFirstFrame() {
    if (firstFrameReported_ || manualStop_ || !pipeline_) {
        return;
    }

    firstFrameReported_ = true;
    reconnectAttempts_ = 0;

    const qint64 elapsed = startupTimer_.isValid() ? startupTimer_.elapsed() : minimumLoadingMsec;
    const int remainingMsec = static_cast<int>(std::max<qint64>(0, minimumLoadingMsec - elapsed));

    emit statusChanged(QString("First frame in %1 ms").arg(elapsed));
    emit firstFrameReceived();

    QTimer::singleShot(remainingMsec, this, [this]() {
        if (!manualStop_ && firstFrameReported_) {
            emit loadingChanged(false);
        }
    });
}

void GstRtspReceiver::checkStall() {
    if (manualStop_ || !pipeline_) {
        return;
    }

    const qint64 startupElapsedMsec = startupTimer_.isValid() ? startupTimer_.elapsed() : 0;

    if (!gotAnyPacket_.load(std::memory_order_relaxed)) {
        if (startupElapsedMsec > initialPacketTimeoutMsec) {
            const QString reason =
                videoPadLinked_
                    ? QString("no RTP packet for %1 ms after H264 pad link").arg(startupElapsedMsec)
                    : QString("no H264 video pad/RTP packet for %1 ms").arg(startupElapsedMsec);

            restartPipeline(reason);
        }

        return;
    }

    if (!gotAnyFrame_.load(std::memory_order_relaxed)) {
        const gint64 lastPacketTime = lastPacketTimeUsec_.load(std::memory_order_relaxed);
        const gint64 elapsedSincePacketMsec = (g_get_monotonic_time() - lastPacketTime) / 1000;

        if (elapsedSincePacketMsec > initialFrameTimeoutMsec) {
            const QString reason = QString("no decoded frame for %1 ms after first RTP packet")
                                       .arg(elapsedSincePacketMsec);

            restartPipeline(reason);
        }

        return;
    }

    const gint64 lastFrameTime = lastFrameTimeUsec_.load(std::memory_order_relaxed);

    if (lastFrameTime <= 0) {
        return;
    }

    const gint64 elapsedMsec = (g_get_monotonic_time() - lastFrameTime) / 1000;

    if (elapsedMsec <= stallTimeoutMsec) {
        return;
    }

    const QString reason = QString("stream stalled for %1 ms").arg(elapsedMsec);

    restartPipeline(reason);
}

GstPadProbeReturn GstRtspReceiver::onFrameProbe(GstPad*, GstPadProbeInfo*, gpointer userData) {
    auto* receiver = static_cast<GstRtspReceiver*>(userData);

    if (!receiver) {
        return GST_PAD_PROBE_OK;
    }

    receiver->lastFrameTimeUsec_.store(g_get_monotonic_time(), std::memory_order_relaxed);

    bool expected = false;
    if (receiver->gotAnyFrame_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                       std::memory_order_relaxed)) {
        QMetaObject::invokeMethod(receiver, [receiver]() { receiver->markFirstFrame(); }, Qt::QueuedConnection);
    }

    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn GstRtspReceiver::onPacketProbe(GstPad*, GstPadProbeInfo*, gpointer userData) {
    auto* receiver = static_cast<GstRtspReceiver*>(userData);

    if (!receiver) {
        return GST_PAD_PROBE_OK;
    }

    receiver->lastPacketTimeUsec_.store(g_get_monotonic_time(), std::memory_order_relaxed);

    bool expected = false;
    if (receiver->gotAnyPacket_.compare_exchange_strong(expected, true, std::memory_order_acq_rel,
                                                        std::memory_order_relaxed)) {
        QMetaObject::invokeMethod(receiver, [receiver]() { receiver->markFirstPacket(); }, Qt::QueuedConnection);
    }

    return GST_PAD_PROBE_OK;
}

gboolean GstRtspReceiver::onSelectStream(GstElement*, guint streamNumber, GstCaps* caps, gpointer) {
    const bool selected = isH264VideoCaps(caps);
    gchar* capsText = caps ? gst_caps_to_string(caps) : g_strdup("(null)");

    qDebug().noquote() << QString("[GstRtspReceiver] select-stream #%1").arg(streamNumber)
                       << (selected ? "H264 video selected" : "ignored") << QString::fromUtf8(capsText);

    g_free(capsText);

    return selected ? TRUE : FALSE;
}

void GstRtspReceiver::onPadAdded(GstElement*, GstPad* pad, gpointer userData) {
    auto* receiver = static_cast<GstRtspReceiver*>(userData);

    if (!receiver || receiver->manualStop_ || !receiver->pipeline_) {
        return;
    }

    if (receiver->videoPadLinked_) {
        return;
    }

    if (!isH264VideoPad(pad)) {
        qDebug().noquote() << "[GstRtspReceiver] Ignored non-H264 RTSP pad";
        return;
    }

    GstElement* videoChain = gst_bin_get_by_name(GST_BIN(receiver->pipeline_), "videochain");

    if (!videoChain) {
        QMetaObject::invokeMethod(receiver, [receiver]() { receiver->errorOccurred("videochain not found"); },
                                  Qt::QueuedConnection);
        return;
    }

    GstPad* chainSinkPad = gst_element_get_static_pad(videoChain, "sink");

    if (!chainSinkPad) {
        gst_object_unref(videoChain);
        QMetaObject::invokeMethod(receiver, [receiver]() { receiver->errorOccurred("videochain sink pad not found"); },
                                  Qt::QueuedConnection);
        return;
    }

    if (gst_pad_is_linked(chainSinkPad)) {
        receiver->videoPadLinked_ = true;
        gst_object_unref(chainSinkPad);
        gst_object_unref(videoChain);
        return;
    }

    const GstPadLinkReturn linkResult = gst_pad_link(pad, chainSinkPad);

    if (GST_PAD_LINK_SUCCESSFUL(linkResult)) {
        receiver->videoPadLinked_ = true;
        QMetaObject::invokeMethod(receiver,
                                  [receiver]() { receiver->statusChanged(QStringLiteral("H264 video pad linked")); },
                                  Qt::QueuedConnection);
    } else {
        const QString errorText = QString("Failed to link H264 video pad: %1").arg(gst_pad_link_get_name(linkResult));
        QMetaObject::invokeMethod(receiver, [receiver, errorText]() { receiver->errorOccurred(errorText); },
                                  Qt::QueuedConnection);
    }

    gst_object_unref(chainSinkPad);
    gst_object_unref(videoChain);
}

gboolean GstRtspReceiver::onBeforeSend(GstElement*, GstRTSPMessage* message, gpointer userData) {
    auto* receiver = static_cast<GstRtspReceiver*>(userData);

    if (!receiver || !receiver->teardownInProgress_ || !message) {
        return TRUE;
    }

    if (gst_rtsp_message_get_type(message) != GST_RTSP_MESSAGE_REQUEST) {
        return TRUE;
    }

    GstRTSPMethod method = GST_RTSP_INVALID;
    const gchar* uri = nullptr;
    GstRTSPVersion version;

    if (gst_rtsp_message_parse_request(message, &method, &uri, &version) != GST_RTSP_OK) {
        return TRUE;
    }

    if (method != GST_RTSP_PAUSE) {
        return TRUE;
    }

    qDebug().noquote() << "[GstRtspReceiver] Drop RTSP PAUSE while tearing down; "
                          "let rtspsrc close with TEARDOWN";
    return FALSE;
}

GstBusSyncReply GstRtspReceiver::onBusSyncMessage(GstBus*, GstMessage* message, gpointer userData) {
    auto* receiver = static_cast<GstRtspReceiver*>(userData);

    if (!receiver || !message) {
        return GST_BUS_PASS;
    }

    if (gst_is_video_overlay_prepare_window_handle_message(message) && receiver->windowHandle_ != 0 &&
        GST_IS_VIDEO_OVERLAY(GST_MESSAGE_SRC(message))) {
        gst_video_overlay_set_window_handle(GST_VIDEO_OVERLAY(GST_MESSAGE_SRC(message)), receiver->windowHandle_);
    }

    return GST_BUS_PASS;
}

QString GstRtspReceiver::decoderChain() const {
    if (hasGstFactory("avdec_h264")) {
        return "avdec_h264 max-threads=2 ! videoconvert n-threads=2 ! video/x-raw,format=BGRx";
    }

    return "d3d11h264dec discard-corrupted-frames=true automatic-request-sync-points=true";
}

void GstRtspReceiver::pollBus() {
    if (!pipeline_) {
        return;
    }

    GstBus* bus = gst_element_get_bus(pipeline_);

    if (!bus) {
        return;
    }

    GstMessage* msg = nullptr;

    while ((msg = gst_bus_pop_filtered(
                bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED |
                                                 GST_MESSAGE_ASYNC_DONE | GST_MESSAGE_LATENCY |
                                                 GST_MESSAGE_ELEMENT))) != nullptr) {
        switch (GST_MESSAGE_TYPE(msg)) {
            case GST_MESSAGE_ERROR: {
                GError* err = nullptr;
                gchar* debugInfo = nullptr;

                gst_message_parse_error(msg, &err, &debugInfo);

                const QString errorText = normalizedGstErrorText(err, debugInfo);

                qDebug().noquote() << "[GStreamer Error]" << errorText;
                emit errorOccurred(errorText);

                if (debugInfo) {
                    qDebug().noquote() << "[GStreamer Debug]" << QString::fromUtf8(debugInfo);
                    g_free(debugInfo);
                }

                if (err) {
                    g_error_free(err);
                }

                gst_message_unref(msg);
                gst_object_unref(bus);

                teardownPipeline();
                scheduleReconnect(errorText,
                                  isAuthenticationFailure(errorText) ? authenticationFailureReconnectDelayMsec : 0);
                return;
            }

            case GST_MESSAGE_EOS:
                qDebug().noquote() << "[GStreamer] End of stream";
                emit statusChanged("End of stream");
                gst_message_unref(msg);
                gst_object_unref(bus);
                teardownPipeline();
                scheduleReconnect("end of stream");
                return;

            case GST_MESSAGE_STATE_CHANGED:
                if (GST_MESSAGE_SRC(msg) == GST_OBJECT(pipeline_)) {
                    GstState oldState;
                    GstState newState;
                    GstState pendingState;

                    gst_message_parse_state_changed(msg, &oldState, &newState, &pendingState);

                    const QString stateText = QString("State: %1").arg(gst_element_state_get_name(newState));

                    qDebug().noquote() << "[GStreamer State]" << stateText;
                    emit statusChanged(stateText);

                    // PLAYING은 파이프라인 상태 전환만 의미하므로 실제 프레임 수신 뒤에 재연결 카운터를 초기화합니다.
                }
                break;

            case GST_MESSAGE_ASYNC_DONE:
                if (!firstAsyncDoneReported_) {
                    firstAsyncDoneReported_ = true;
                    emit statusChanged(QString("First async done in %1 ms").arg(startupTimer_.elapsed()));
                }
                break;

            case GST_MESSAGE_LATENCY:
                gst_bin_recalculate_latency(GST_BIN(pipeline_));
                break;

            case GST_MESSAGE_ELEMENT: {
                const GstStructure* structure = gst_message_get_structure(msg);

                if (structure && gst_structure_has_name(structure, "GstRTSPSrcTimeout")) {
                    gchar* detail = gst_structure_to_string(structure);
                    const QString timeoutDetail = detail ? QString::fromUtf8(detail) : QStringLiteral("unknown");
                    const QString reason = QString("RTSP timeout: %1").arg(timeoutDetail);

                    qDebug().noquote() << "[GStreamer RTSP Timeout]" << reason;
                    emit statusChanged(reason);

                    if (detail) {
                        g_free(detail);
                    }

                    gst_message_unref(msg);
                    gst_object_unref(bus);
                    teardownPipeline();
                    scheduleReconnect(reason);
                    return;
                }

                break;
            }

            default:
                break;
        }

        gst_message_unref(msg);
    }

    gst_object_unref(bus);
    checkStall();
}
