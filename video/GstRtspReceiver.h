#pragma once

#include <gst/gst.h>
#include <gst/rtsp/gstrtspmessage.h>

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QWidget>
#include <atomic>

/** 단일 RTSP 카메라 스트림을 GStreamer로 수신해 네이티브 QWidget에 출력합니다. */
class GstRtspReceiver : public QObject {
    Q_OBJECT

public:
    /** 영상이 렌더링될 대상 위젯을 받아 수신기 상태와 타이머를 준비합니다. */
    explicit GstRtspReceiver(QWidget* outputWidget, QObject* parent = nullptr);
    ~GstRtspReceiver() override;

    /** start() 호출 전에 사용할 RTSP 주소를 설정합니다. */
    void setUrl(const QString& url);

    /** GStreamer RTSP 파이프라인을 시작하거나 재시작합니다. */
    void start();

    /** 재생을 중지하고 예약된 재연결 시도를 취소합니다. */
    void stop();

    /** 첫 프레임 수신을 기록하고 최소 로딩 표시 시간 이후 오버레이를 숨깁니다. */
    void markFirstFrame();

signals:
    /** 연결, 재연결, 첫 프레임 등 사용자에게 보여줄 수 있는 상태 문자열을 알립니다. */
    void statusChanged(const QString& status);

    /** 파이프라인 생성, RTSP 연결, 영상 출력 중 발생한 오류를 알립니다. */
    void errorOccurred(const QString& error);

    /** 비디오 셀의 로딩 오버레이 표시 여부를 알립니다. */
    void loadingChanged(bool loading);

    /** RTSP 비디오 데이터가 처음 들어왔음을 알립니다. */
    void streamDataReceived();

    /** sink 직전의 실제 영상 프레임이 처음 도착했음을 알립니다. */
    void firstFrameReceived();

private slots:
    /** GStreamer bus 메시지를 Qt 이벤트 루프에서 주기적으로 처리합니다. */
    void pollBus();

private:
    /** 현재 설정으로 파이프라인을 만들고 재생 상태로 전환합니다. */
    void startPipeline();

    /** bus 핸들러와 파이프라인 상태를 정리하고 GStreamer 객체를 해제합니다. */
    void teardownPipeline();

    /** 오류나 프레임 정지 상황에서 지수 백오프 방식으로 재연결을 예약합니다. */
    void scheduleReconnect(const QString& reason);

    /** 현재 파이프라인을 정리하고 같은 RTSP 주소로 재연결을 예약합니다. */
    void restartPipeline(const QString& reason);

    /** RTSP URL의 인증 정보와 주소를 rtspsrc 속성으로 분리해 적용합니다. */
    bool applySourceProperties(GstElement* source);

    /** 사용 가능한 H.264 디코더에 맞는 GStreamer 디코더 체인을 반환합니다. */
    QString decoderChain() const;

    /** 첫 버퍼 또는 이후 버퍼가 오래 들어오지 않는 상황을 감지합니다. */
    void checkStall();

    /** 첫 RTSP 비디오 데이터 수신을 기록하고 다음 채널 시작 기준 신호를 알립니다. */
    void markFirstPacket();

    /** sink 직전 영상 버퍼를 감시해 실제 출력 프레임 유입 시간을 기록합니다. */
    static GstPadProbeReturn onFrameProbe(GstPad* pad, GstPadProbeInfo* info, gpointer userData);

    /** depayloader 입력 버퍼를 감시해 네트워크/RTSP 비디오 데이터 유입 시간을 기록합니다. */
    static GstPadProbeReturn onPacketProbe(GstPad* pad, GstPadProbeInfo* info, gpointer userData);

    /** rtspsrc의 SDP stream 중 H.264 영상 스트림만 선택합니다. */
    static gboolean onSelectStream(GstElement* source, guint streamNumber, GstCaps* caps, gpointer userData);

    /** rtspsrc가 만든 동적 pad 중 H.264 영상 pad만 depayloader에 연결합니다. */
    static void onPadAdded(GstElement* source, GstPad* pad, gpointer userData);

    /** 종료 중 rtspsrc가 TEARDOWN 대신 PAUSE로 RTSP 세션을 남기는 요청을 막습니다. */
    static gboolean onBeforeSend(GstElement* source, GstRTSPMessage* message, gpointer userData);

    /** GStreamer 영상 출력 sink가 요청하는 네이티브 창 핸들을 전달합니다. */
    static GstBusSyncReply onBusSyncMessage(GstBus* bus, GstMessage* message, gpointer userData);

    QWidget* outputWidget_ = nullptr;
    QString url_;

    GstElement* pipeline_ = nullptr;
    QTimer busTimer_;
    QTimer reconnectTimer_;

    QElapsedTimer startupTimer_;
    std::atomic<gint64> lastPacketTimeUsec_{0};
    std::atomic<gint64> lastFrameTimeUsec_{0};
    std::atomic_bool gotAnyPacket_{false};
    std::atomic_bool gotAnyFrame_{false};
    guintptr windowHandle_ = 0;
    int reconnectAttempts_ = 0;
    bool videoPadLinked_ = false;
    bool manualStop_ = true;
    bool teardownInProgress_ = false;
    bool firstAsyncDoneReported_ = false;
    bool firstFrameReported_ = false;
};
