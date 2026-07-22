#pragma once

#include <gst/gst.h>
#include <gst/rtsp/gstrtspmessage.h>

#include <QElapsedTimer>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QVector>
#include <atomic>
#include <vector>

#include "video/StreamReceiver.h"

class QThread;
class QTimer;

class GstRtspReceiver : public StreamReceiver {
    Q_OBJECT

public:
    explicit GstRtspReceiver(guintptr outputWindowHandle, QObject* parent = nullptr);
    ~GstRtspReceiver() override;

    void setUrl(const QString& url) override;
    void setHeadBlurFrame(HeadBlurFrameData frame) override;
    void moveInternalObjectsToThread(QThread* thread) override;
    void start() override;
    void stop() override;
    void markFirstFrame();

private slots:
    void pollBus();

private:
    void startPipeline();
    void teardownPipeline();
    void scheduleReconnect(const QString& reason, int overrideDelayMsec = 0);
    void restartPipeline(const QString& reason);
    bool applySourceProperties(GstElement* source);
    QString decoderChain() const;
    void checkStall();

    void markFirstPacket();
    QVector<QRectF> headBlurRegionsFor(qint64 sourceTimestamp) const;
    void applyHeadBlur(GstPad* pad, GstPadProbeInfo* info);

    static GstPadProbeReturn onFrameProbe(GstPad* pad, GstPadProbeInfo* info, gpointer userData);

    static GstPadProbeReturn onPacketProbe(GstPad* pad, GstPadProbeInfo* info, gpointer userData);

    static gboolean onSelectStream(GstElement* source, guint streamNumber, GstCaps* caps, gpointer userData);

    static void onPadAdded(GstElement* source, GstPad* pad, gpointer userData);

    static gboolean onBeforeSend(GstElement* source, GstRTSPMessage* message, gpointer userData);

    static GstBusSyncReply onBusSyncMessage(GstBus* bus, GstMessage* message, gpointer userData);

private:
    guintptr outputWindowHandle_ = 0;
    QString url_;

    GstElement* pipeline_ = nullptr;
    QTimer* busTimer_ = nullptr;
    QTimer* reconnectTimer_ = nullptr;

    QElapsedTimer startupTimer_;

    std::atomic<gint64> firstPacketTimeUsec_{0};
    std::atomic<gint64> lastPacketTimeUsec_{0};
    std::atomic<gint64> lastFrameTimeUsec_{0};
    std::atomic_bool gotAnyPacket_{false};
    std::atomic_bool gotAnyFrame_{false};

    guintptr windowHandle_ = 0;
    int reconnectAttempts_ = 0;

    std::atomic_bool videoPadLinked_{false};
    std::atomic_bool manualStop_{true};
    std::atomic_bool teardownInProgress_{false};

    bool firstAsyncDoneReported_ = false;
    bool firstFrameReported_ = false;

    mutable QMutex headBlurMutex_;
    QVector<HeadBlurFrameData> headBlurHistory_;
    std::vector<guint8> headBlurScratch_;
    std::atomic<qint64> headBlurSourceToLocalOffsetMsec_{0};
    std::atomic_bool headBlurClockOffsetReady_{false};
    std::atomic<qint64> lastHeadBlurApplyLogMsec_{0};
};
