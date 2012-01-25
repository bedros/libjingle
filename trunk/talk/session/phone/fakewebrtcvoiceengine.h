/*
 * libjingle
 * Copyright 2010, Google Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TALK_SESSION_PHONE_FAKEWEBRTCVOICEENGINE_H_
#define TALK_SESSION_PHONE_FAKEWEBRTCVOICEENGINE_H_

#include <list>
#include <map>
#include <vector>

#include "talk/base/basictypes.h"
#include "talk/base/stringutils.h"
#include "talk/session/phone/codec.h"
#include "talk/session/phone/fakewebrtccommon.h"
#include "talk/session/phone/voiceprocessor.h"
#include "talk/session/phone/webrtcvoe.h"

namespace cricket {

static const char kFakeDefaultDeviceName[] = "Fake Default";
static const int kFakeDefaultDeviceId = -1;
static const char kFakeDeviceName[] = "Fake Device";
#ifdef WIN32
static const int kFakeDeviceId = 0;
#else
static const int kFakeDeviceId = 1;
#endif

class FakeWebRtcVoiceEngine
    : public webrtc::VoEAudioProcessing,
      public webrtc::VoEBase, public webrtc::VoECodec, public webrtc::VoEDtmf,
      public webrtc::VoEFile, public webrtc::VoEHardware,
      public webrtc::VoEExternalMedia, public webrtc::VoENetEqStats,
      public webrtc::VoENetwork, public webrtc::VoERTP_RTCP,
      public webrtc::VoEVideoSync, public webrtc::VoEVolumeControl {
 public:
  struct Channel {
    Channel()
        : external_transport(false),
          send(false),
          playout(false),
          file(false),
          vad(false),
          fec(false),
          cn8_type(13),
          cn16_type(105),
          dtmf_type(106),
          fec_type(117),
          send_ssrc(0),
          level_header_ext_(-1) {
      memset(&send_codec, 0, sizeof(send_codec));
    }
    bool external_transport;
    bool send;
    bool playout;
    bool file;
    bool vad;
    bool fec;
    int cn8_type;
    int cn16_type;
    int dtmf_type;
    int fec_type;
    uint32 send_ssrc;
    int level_header_ext_;
    std::vector<webrtc::CodecInst> recv_codecs;
    webrtc::CodecInst send_codec;
    std::list<std::string> packets;
  };

  FakeWebRtcVoiceEngine(const cricket::AudioCodec* const* codecs,
                        int num_codecs)
      : inited_(false),
        last_channel_(-1),
        fail_create_channel_(false),
        codecs_(codecs),
        num_codecs_(num_codecs),
        ec_enabled_(false),
        ns_enabled_(false),
        ec_mode_(webrtc::kEcDefault),
        ns_mode_(webrtc::kNsDefault),
        observer_(NULL),
        playout_fail_channel_(-1),
        send_fail_channel_(-1),
        fail_start_recording_microphone_(false),
        recording_microphone_(false),
        media_processor_(NULL) {
    memset(&agc_config_, 0, sizeof(agc_config_));
  }
  ~FakeWebRtcVoiceEngine() {
    // Ought to have all been deleted by the WebRtcVoiceMediaChannel
    // destructors, but just in case ...
    for (std::map<int, Channel*>::const_iterator i = channels_.begin();
         i != channels_.end(); ++i) {
      delete i->second;
    }
  }
  bool IsExternalMediaProcessorRegistered() const {
    return media_processor_ != NULL;
  }
  bool IsInited() const { return inited_; }
  int GetLastChannel() const { return last_channel_; }
  int GetNumChannels() const { return channels_.size(); }
  bool GetPlayout(int channel) {
    return channels_[channel]->playout;
  }
  bool GetSend(int channel) {
    return channels_[channel]->send;
  }
  bool GetRecordingMicrophone() {
    return recording_microphone_;
  }
  bool GetVAD(int channel) {
    return channels_[channel]->vad;
  }
  bool GetFEC(int channel) {
    return channels_[channel]->fec;
  }
  int GetSendCNPayloadType(int channel, bool wideband) {
    return (wideband) ?
        channels_[channel]->cn16_type :
        channels_[channel]->cn8_type;
  }
  int GetSendTelephoneEventPayloadType(int channel) {
    return channels_[channel]->dtmf_type;
  }
  int GetSendFECPayloadType(int channel) {
    return channels_[channel]->fec_type;
  }
  bool CheckPacket(int channel, const void* data, size_t len) {
    bool result = !CheckNoPacket(channel);
    if (result) {
      std::string packet = channels_[channel]->packets.front();
      result = (packet == std::string(static_cast<const char*>(data), len));
      channels_[channel]->packets.pop_front();
    }
    return result;
  }
  bool CheckNoPacket(int channel) {
    return channels_[channel]->packets.empty();
  }
  void TriggerCallbackOnError(int channel_num, int err_code) {
    ASSERT(observer_ != NULL);
    observer_->CallbackOnError(channel_num, err_code);
  }
  void set_playout_fail_channel(int channel) {
    playout_fail_channel_ = channel;
  }
  void set_send_fail_channel(int channel) {
    send_fail_channel_ = channel;
  }
  void set_fail_start_recording_microphone(
      bool fail_start_recording_microphone) {
    fail_start_recording_microphone_ = fail_start_recording_microphone;
  }
  void set_fail_create_channel(bool fail_create_channel) {
    fail_create_channel_ = fail_create_channel;
  }
  void TriggerProcessPacket(MediaProcessorDirection direction) {
    webrtc::ProcessingTypes pt =
        (direction == cricket::MPD_TX) ?
            webrtc::kRecordingPerChannel : webrtc::kPlaybackAllChannelsMixed;
    if (media_processor_ != NULL) {
      media_processor_->Process(0,
                                pt,
                                NULL,
                                0,
                                0,
                                true);
    }
  }

  WEBRTC_STUB(Release, ());

  // webrtc::VoEBase
  WEBRTC_FUNC(RegisterVoiceEngineObserver, (
      webrtc::VoiceEngineObserver& observer)) {
    observer_ = &observer;
    return 0;
  }
  WEBRTC_STUB(DeRegisterVoiceEngineObserver, ());
  WEBRTC_STUB(RegisterAudioDeviceModule, (webrtc::AudioDeviceModule& adm));
  WEBRTC_STUB(DeRegisterAudioDeviceModule, ());

  WEBRTC_FUNC(Init, (webrtc::AudioDeviceModule* adm)) {
    inited_ = true;
    return 0;
  }
  WEBRTC_FUNC(Terminate, ()) {
    inited_ = false;
    return 0;
  }
  WEBRTC_STUB(MaxNumOfChannels, ());
  WEBRTC_FUNC(CreateChannel, ()) {
    if (fail_create_channel_) {
      return -1;
    }
    Channel* ch = new Channel();
    for (int i = 0; i < NumOfCodecs(); ++i) {
      webrtc::CodecInst codec;
      GetCodec(i, codec);
      ch->recv_codecs.push_back(codec);
    }
    channels_[++last_channel_] = ch;
    return last_channel_;
  }
  WEBRTC_FUNC(DeleteChannel, (int channel)) {
    WEBRTC_CHECK_CHANNEL(channel);
    delete channels_[channel];
    channels_.erase(channel);
    return 0;
  }
  WEBRTC_STUB(SetLocalReceiver, (int channel, int port, int RTCPport,
                                 const char ipaddr[64],
                                 const char multiCastAddr[64]));
  WEBRTC_STUB(GetLocalReceiver, (int channel, int& port, int& RTCPport,
                                 char ipaddr[64]));
  WEBRTC_STUB(SetSendDestination, (int channel, int port,
                                   const char ipaddr[64],
                                   int sourcePort, int RTCPport));
  WEBRTC_STUB(GetSendDestination, (int channel, int& port, char ipaddr[64],
                                   int& sourcePort, int& RTCPport));
  WEBRTC_STUB(StartReceive, (int channel));
  WEBRTC_FUNC(StartPlayout, (int channel)) {
    if (playout_fail_channel_ != channel) {
      WEBRTC_CHECK_CHANNEL(channel);
      channels_[channel]->playout = true;
      return 0;
    } else {
      // When playout_fail_channel_ == channel, fail the StartPlayout on this
      // channel.
      return -1;
    }
  }
  WEBRTC_FUNC(StartSend, (int channel)) {
    if (send_fail_channel_ != channel) {
      WEBRTC_CHECK_CHANNEL(channel);
      channels_[channel]->send = true;
      return 0;
    } else {
      // When send_fail_channel_ == channel, fail the StartSend on this
      // channel.
      return -1;
    }
  }
  WEBRTC_STUB(StopReceive, (int channel));
  WEBRTC_FUNC(StopPlayout, (int channel)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->playout = false;
    return 0;
  }
  WEBRTC_FUNC(StopSend, (int channel)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->send = false;
    return 0;
  }
  WEBRTC_STUB(GetVersion, (char version[1024]));
  WEBRTC_STUB(LastError, ());
  WEBRTC_STUB(SetOnHoldStatus, (int, bool, webrtc::OnHoldModes));
  WEBRTC_STUB(GetOnHoldStatus, (int, bool&, webrtc::OnHoldModes&));
  WEBRTC_STUB(SetNetEQPlayoutMode, (int, webrtc::NetEqModes));
  WEBRTC_STUB(GetNetEQPlayoutMode, (int, webrtc::NetEqModes&));
  WEBRTC_STUB(SetNetEQBGNMode, (int, webrtc::NetEqBgnModes));
  WEBRTC_STUB(GetNetEQBGNMode, (int, webrtc::NetEqBgnModes&));

  // webrtc::VoECodec
  WEBRTC_FUNC(NumOfCodecs, ()) {
    return num_codecs_;
  }
  WEBRTC_FUNC(GetCodec, (int index, webrtc::CodecInst& codec)) {
    if (index < 0 || index >= NumOfCodecs()) {
      return -1;
    }
    const cricket::AudioCodec& c(*codecs_[index]);
    codec.pltype = c.id;
    talk_base::strcpyn(codec.plname, sizeof(codec.plname), c.name.c_str());
    codec.plfreq = c.clockrate;
    codec.pacsize = 0;
    codec.channels = c.channels;
    codec.rate = c.bitrate;
    return 0;
  }
  WEBRTC_FUNC(SetSendCodec, (int channel, const webrtc::CodecInst& codec)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->send_codec = codec;
    return 0;
  }
  WEBRTC_FUNC(GetSendCodec, (int channel, webrtc::CodecInst& codec)) {
    WEBRTC_CHECK_CHANNEL(channel);
    codec = channels_[channel]->send_codec;
    return 0;
  }
  WEBRTC_STUB(GetRecCodec, (int channel, webrtc::CodecInst& codec));
  WEBRTC_STUB(SetAMREncFormat, (int channel, webrtc::AmrMode mode));
  WEBRTC_STUB(SetAMRDecFormat, (int channel, webrtc::AmrMode mode));
  WEBRTC_STUB(SetAMRWbEncFormat, (int channel, webrtc::AmrMode mode));
  WEBRTC_STUB(SetAMRWbDecFormat, (int channel, webrtc::AmrMode mode));
  WEBRTC_STUB(SetISACInitTargetRate, (int channel, int rateBps,
                                      bool useFixedFrameSize));
  WEBRTC_STUB(SetISACMaxRate, (int channel, int rateBps));
  WEBRTC_STUB(SetISACMaxPayloadSize, (int channel, int sizeBytes));
  WEBRTC_FUNC(SetRecPayloadType, (int channel,
                                  const webrtc::CodecInst& codec)) {
    WEBRTC_CHECK_CHANNEL(channel);
    Channel* ch = channels_[channel];
    // Check if something else already has this slot.
    if (codec.pltype != -1) {
      for (std::vector<webrtc::CodecInst>::iterator it =
          ch->recv_codecs.begin(); it != ch->recv_codecs.end(); ++it) {
        if (it->pltype == codec.pltype) {
          return -1;
        }
      }
    }
    // Otherwise try to find this codec and update its payload type.
    for (std::vector<webrtc::CodecInst>::iterator it = ch->recv_codecs.begin();
         it != ch->recv_codecs.end(); ++it) {
      if (strcmp(it->plname, codec.plname) == 0 &&
          it->plfreq == codec.plfreq) {
        it->pltype = codec.pltype;
        return 0;
      }
    }
    return -1;  // not found
  }
  WEBRTC_FUNC(SetSendCNPayloadType, (int channel, int type,
                                     webrtc::PayloadFrequencies frequency)) {
    WEBRTC_CHECK_CHANNEL(channel);
    if (frequency == webrtc::kFreq8000Hz) {
      channels_[channel]->cn8_type = type;
    } else if (frequency == webrtc::kFreq16000Hz) {
      channels_[channel]->cn16_type = type;
    }
    return 0;
  }
  WEBRTC_FUNC(GetRecPayloadType, (int channel, webrtc::CodecInst& codec)) {
    WEBRTC_CHECK_CHANNEL(channel);
    Channel* ch = channels_[channel];
    for (std::vector<webrtc::CodecInst>::iterator it = ch->recv_codecs.begin();
         it != ch->recv_codecs.end(); ++it) {
      if (strcmp(it->plname, codec.plname) == 0 &&
          it->plfreq == codec.plfreq &&
          it->pltype != -1) {
        codec.pltype = it->pltype;
        return 0;
      }
    }
    return -1;  // not found
  }
  WEBRTC_FUNC(SetVADStatus, (int channel, bool enable, webrtc::VadModes mode,
                             bool disableDTX)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->vad = enable;
    return 0;
  }
  WEBRTC_STUB(GetVADStatus, (int channel, bool& enabled,
                             webrtc::VadModes& mode, bool& disabledDTX));

  // webrtc::VoEDtmf
  WEBRTC_STUB(SendTelephoneEvent, (int channel, int eventCode,
      bool outOfBand = true, int lengthMs = 160, int attenuationDb = 10));

  WEBRTC_FUNC(SetSendTelephoneEventPayloadType,
      (int channel, unsigned char type)) {
    channels_[channel]->dtmf_type = type;
    return 0;
  };
  WEBRTC_STUB(GetSendTelephoneEventPayloadType,
      (int channel, unsigned char& type));

  WEBRTC_STUB(SetDtmfFeedbackStatus, (bool enable, bool directFeedback));
  WEBRTC_STUB(GetDtmfFeedbackStatus, (bool& enabled, bool& directFeedback));
  WEBRTC_STUB(RegisterTelephoneEventDetection, (int channel,
      webrtc::TelephoneEventDetectionMethods detectionMethod,
      webrtc::VoETelephoneEventObserver& observer));
  WEBRTC_STUB(DeRegisterTelephoneEventDetection, (int channel));
  WEBRTC_STUB(SetDtmfPlayoutStatus, (int channel, bool enable));
  WEBRTC_STUB(GetDtmfPlayoutStatus, (int channel, bool& enabled));


  WEBRTC_STUB(PlayDtmfTone,
      (int eventCode, int lengthMs = 200, int attenuationDb = 10));
  WEBRTC_STUB(StartPlayingDtmfTone,
      (int eventCode, int attenuationDb = 10));
  WEBRTC_STUB(StopPlayingDtmfTone, ());
  WEBRTC_STUB(GetTelephoneEventDetectionStatus, (int channel,
      bool& enabled, webrtc::TelephoneEventDetectionMethods& detectionMethod));

  // webrtc::VoEFile
  WEBRTC_FUNC(StartPlayingFileLocally, (int channel, const char* fileNameUTF8,
                                        bool loop, webrtc::FileFormats format,
                                        float volumeScaling, int startPointMs,
                                        int stopPointMs)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->file = true;
    return 0;
  }
  WEBRTC_FUNC(StartPlayingFileLocally, (int channel, webrtc::InStream* stream,
                                        webrtc::FileFormats format,
                                        float volumeScaling, int startPointMs,
                                        int stopPointMs)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->file = true;
    return 0;
  }
  WEBRTC_FUNC(StopPlayingFileLocally, (int channel)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->file = false;
    return 0;
  }
  WEBRTC_FUNC(IsPlayingFileLocally, (int channel)) {
    WEBRTC_CHECK_CHANNEL(channel);
    return (channels_[channel]->file) ? 1 : 0;
  }
  WEBRTC_STUB(ScaleLocalFilePlayout, (int channel, float scale));
  WEBRTC_STUB(StartPlayingFileAsMicrophone, (int channel,
                                             const char* fileNameUTF8,
                                             bool loop,
                                             bool mixWithMicrophone,
                                             webrtc::FileFormats format,
                                             float volumeScaling));
  WEBRTC_STUB(StartPlayingFileAsMicrophone, (int channel,
                                             webrtc::InStream* stream,
                                             bool mixWithMicrophone,
                                             webrtc::FileFormats format,
                                             float volumeScaling));
  WEBRTC_STUB(StopPlayingFileAsMicrophone, (int channel));
  WEBRTC_STUB(IsPlayingFileAsMicrophone, (int channel));
  WEBRTC_STUB(ScaleFileAsMicrophonePlayout, (int channel, float scale));
  WEBRTC_STUB(StartRecordingPlayout, (int channel, const char* fileNameUTF8,
                                      webrtc::CodecInst* compression,
                                      int maxSizeBytes));
  WEBRTC_STUB(StartRecordingPlayout, (int channel, webrtc::OutStream* stream,
                                      webrtc::CodecInst* compression));
  WEBRTC_STUB(StopRecordingPlayout, (int channel));
  WEBRTC_FUNC(StartRecordingMicrophone, (const char* fileNameUTF8,
                                         webrtc::CodecInst* compression,
                                         int maxSizeBytes)) {
    if (fail_start_recording_microphone_) {
      return -1;
    }
    recording_microphone_ = true;
    return 0;
  }
  WEBRTC_FUNC(StartRecordingMicrophone, (webrtc::OutStream* stream,
                                         webrtc::CodecInst* compression)) {
    if (fail_start_recording_microphone_) {
      return -1;
    }
    recording_microphone_ = true;
    return 0;
  }
  WEBRTC_FUNC(StopRecordingMicrophone, ()) {
    if (!recording_microphone_) {
      return -1;
    }
    recording_microphone_ = false;
    return 0;
  }
  WEBRTC_STUB(ConvertPCMToWAV, (const char* fileNameInUTF8,
                                const char* fileNameOutUTF8));
  WEBRTC_STUB(ConvertPCMToWAV, (webrtc::InStream* streamIn,
                                webrtc::OutStream* streamOut));
  WEBRTC_STUB(ConvertWAVToPCM, (const char* fileNameInUTF8,
                                const char* fileNameOutUTF8));
  WEBRTC_STUB(ConvertWAVToPCM, (webrtc::InStream* streamIn,
                                webrtc::OutStream* streamOut));
  WEBRTC_STUB(ConvertPCMToCompressed, (const char* fileNameInUTF8,
                                       const char* fileNameOutUTF8,
                                       webrtc::CodecInst* compression));
  WEBRTC_STUB(ConvertPCMToCompressed, (webrtc::InStream* streamIn,
                                       webrtc::OutStream* streamOut,
                                       webrtc::CodecInst* compression));
  WEBRTC_STUB(ConvertCompressedToPCM, (const char* fileNameInUTF8,
                                     const char* fileNameOutUTF8));
  WEBRTC_STUB(ConvertCompressedToPCM, (webrtc::InStream* streamIn,
                                       webrtc::OutStream* streamOut));
  WEBRTC_STUB(GetFileDuration, (const char* fileNameUTF8, int& durationMs,
                                webrtc::FileFormats format));
  WEBRTC_STUB(GetPlaybackPosition, (int channel, int& positionMs));

  // webrtc::VoEHardware
  WEBRTC_STUB(GetCPULoad, (int&));
  WEBRTC_STUB(GetSystemCPULoad, (int&));
  WEBRTC_FUNC(GetNumOfRecordingDevices, (int& num)) {
    return GetNumDevices(num);
  }
  WEBRTC_FUNC(GetNumOfPlayoutDevices, (int& num)) {
    return GetNumDevices(num);
  }
  WEBRTC_FUNC(GetRecordingDeviceName, (int i, char* name, char* guid)) {
    return GetDeviceName(i, name, guid);
  }
  WEBRTC_FUNC(GetPlayoutDeviceName, (int i, char* name, char* guid)) {
    return GetDeviceName(i, name, guid);
  }
  WEBRTC_STUB(SetRecordingDevice, (int, webrtc::StereoChannel));
  WEBRTC_STUB(SetPlayoutDevice, (int));
  WEBRTC_STUB(SetAudioDeviceLayer, (webrtc::AudioLayers));
  WEBRTC_STUB(GetAudioDeviceLayer, (webrtc::AudioLayers&));
  WEBRTC_STUB(GetPlayoutDeviceStatus, (bool&));
  WEBRTC_STUB(GetRecordingDeviceStatus, (bool&));
  WEBRTC_STUB(ResetAudioDevice, ());
  WEBRTC_STUB(AudioDeviceControl, (unsigned int, unsigned int, unsigned int));
  WEBRTC_STUB(NeedMorePlayData, (short int*, int, int, int, int&));
  WEBRTC_STUB(RecordedDataIsAvailable, (short int*, int, int, int, int&));
  WEBRTC_STUB(GetDevice, (char*, unsigned int));
  WEBRTC_STUB(GetPlatform, (char*, unsigned int));
  WEBRTC_STUB(GetOS, (char*, unsigned int));
  WEBRTC_STUB(SetGrabPlayout, (bool));
  WEBRTC_STUB(SetGrabRecording, (bool));
  WEBRTC_STUB(SetLoudspeakerStatus, (bool enable));
  WEBRTC_STUB(GetLoudspeakerStatus, (bool& enabled));
  WEBRTC_STUB(EnableBuiltInAEC, (bool enable));
  virtual bool BuiltInAECIsEnabled() const { return true; }
  WEBRTC_STUB(SetSamplingRate, (int));
  WEBRTC_STUB(GetSamplingRate, (int&));

  // webrtc::VoENetEqStats
  WEBRTC_STUB(GetNetworkStatistics, (int, webrtc::NetworkStatistics&));
  WEBRTC_STUB(GetPreferredBufferSize, (int, short unsigned int&));
  WEBRTC_STUB(ResetJitterStatistics, (int));

  // webrtc::VoENetwork
  WEBRTC_FUNC(RegisterExternalTransport, (int channel,
                                          webrtc::Transport& transport)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->external_transport = true;
    return 0;
  }
  WEBRTC_FUNC(DeRegisterExternalTransport, (int channel)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->external_transport = false;
    return 0;
  }
  WEBRTC_FUNC(ReceivedRTPPacket, (int channel, const void* data,
                                  unsigned int length)) {
    WEBRTC_CHECK_CHANNEL(channel);
    if (!channels_[channel]->external_transport) return -1;
    channels_[channel]->packets.push_back(
        std::string(static_cast<const char*>(data), length));
    return 0;
  }
  WEBRTC_STUB(ReceivedRTCPPacket, (int channel, const void* data,
                                   unsigned int length));
  WEBRTC_STUB(GetSourceInfo, (int channel, int& rtpPort, int& rtcpPort,
                              char ipaddr[64]));
  WEBRTC_STUB(GetLocalIP, (char ipaddr[64], bool ipv6));
  WEBRTC_STUB(EnableIPv6, (int channel));
  // Not using WEBRTC_STUB due to bool return value
  virtual bool IPv6IsEnabled(int channel) { return true; }
  WEBRTC_STUB(SetSourceFilter, (int channel, int rtpPort, int rtcpPort,
                                const char ipaddr[64]));
  WEBRTC_STUB(GetSourceFilter, (int channel, int& rtpPort, int& rtcpPort,
                                char ipaddr[64]));
  WEBRTC_STUB(SetSendTOS, (int channel, int priority,
                           int DSCP, bool useSetSockopt));
  WEBRTC_STUB(GetSendTOS, (int channel, int& priority,
                           int& DSCP, bool& useSetSockopt));
  WEBRTC_STUB(SetSendGQoS, (int channel, bool enable, int serviceType,
                            int overrideDSCP));
  WEBRTC_STUB(GetSendGQoS, (int channel, bool& enabled, int& serviceType,
                            int& overrideDSCP));
  WEBRTC_STUB(SetPacketTimeoutNotification, (int channel, bool enable,
                                             int timeoutSeconds));
  WEBRTC_STUB(GetPacketTimeoutNotification, (int channel, bool& enable,
                                             int& timeoutSeconds));
  WEBRTC_STUB(RegisterDeadOrAliveObserver, (int channel,
      webrtc::VoEConnectionObserver& observer));
  WEBRTC_STUB(DeRegisterDeadOrAliveObserver, (int channel));
  WEBRTC_STUB(GetPeriodicDeadOrAliveStatus, (int channel, bool& enabled,
                                             int& sampleTimeSeconds));
  WEBRTC_STUB(SetPeriodicDeadOrAliveStatus, (int channel, bool enable,
                                             int sampleTimeSeconds));
  WEBRTC_STUB(SendUDPPacket, (int channel, const void* data,
                              unsigned int length, int& transmittedBytes,
                              bool useRtcpSocket));

  // webrtc::VoERTP_RTCP
  WEBRTC_STUB(RegisterRTPObserver, (int channel,
                                    webrtc::VoERTPObserver& observer));
  WEBRTC_STUB(DeRegisterRTPObserver, (int channel));
  WEBRTC_STUB(RegisterRTCPObserver, (int channel,
                                     webrtc::VoERTCPObserver& observer));
  WEBRTC_STUB(DeRegisterRTCPObserver, (int channel));
  WEBRTC_FUNC(SetLocalSSRC, (int channel, unsigned int ssrc)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->send_ssrc = ssrc;
    return 0;
  }
  WEBRTC_FUNC(GetLocalSSRC, (int channel, unsigned int& ssrc)) {
    WEBRTC_CHECK_CHANNEL(channel);
    ssrc = channels_[channel]->send_ssrc;
    return 0;
  }
  WEBRTC_STUB(GetRemoteSSRC, (int channel, unsigned int& ssrc));
  WEBRTC_FUNC(SetRTPAudioLevelIndicationStatus, (int channel, bool enable,
      unsigned char ID)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->level_header_ext_ = (enable) ? ID : -1;
    return 0;
  }
  WEBRTC_FUNC(GetRTPAudioLevelIndicationStatus, (int channel, bool& enabled,
      unsigned char& ID)) {
    WEBRTC_CHECK_CHANNEL(channel);
    enabled = (channels_[channel]->level_header_ext_ != -1);
    ID = channels_[channel]->level_header_ext_;
    return 0;
  }
  WEBRTC_STUB(GetRemoteCSRCs, (int channel, unsigned int arrCSRC[15]));
  WEBRTC_STUB(GetRemoteEnergy, (int channel, unsigned char arrEnergy[15]));
  WEBRTC_STUB(SetRTCPStatus, (int channel, bool enable));
  WEBRTC_STUB(GetRTCPStatus, (int channel, bool& enabled));
  WEBRTC_STUB(SetRTCP_CNAME, (int channel, const char cname[256]));
  WEBRTC_STUB(GetRTCP_CNAME, (int channel, char cname[256]));
  WEBRTC_STUB(GetRemoteRTCP_CNAME, (int channel, char* cname));
  WEBRTC_STUB(GetRemoteRTCPData, (int channel, unsigned int& NTPHigh,
                                  unsigned int& NTPLow,
                                  unsigned int& timestamp,
                                  unsigned int& playoutTimestamp,
                                  unsigned int* jitter,
                                  unsigned short* fractionLost));
  WEBRTC_STUB(SendApplicationDefinedRTCPPacket, (int channel,
                                                 const unsigned char subType,
                                                 unsigned int name,
                                                 const char* data,
                                                 unsigned short dataLength));
  WEBRTC_STUB(GetRTPStatistics, (int channel, unsigned int& averageJitterMs,
                                 unsigned int& maxJitterMs,
                                 unsigned int& discardedPackets));
  WEBRTC_STUB(GetRTCPStatistics, (int channel, unsigned short& fractionLost,
                                  unsigned int& cumulativeLost,
                                  unsigned int& extendedMax,
                                  unsigned int& jitterSamples, int& rttMs));
  WEBRTC_STUB(GetRTCPStatistics, (int channel, webrtc::CallStatistics& stats));
  WEBRTC_FUNC(SetFECStatus, (int channel, bool enable, int redPayloadtype)) {
    WEBRTC_CHECK_CHANNEL(channel);
    channels_[channel]->fec = enable;
    channels_[channel]->fec_type = redPayloadtype;
    return 0;
  }
  WEBRTC_FUNC(GetFECStatus, (int channel, bool& enable, int& redPayloadtype)) {
    WEBRTC_CHECK_CHANNEL(channel);
    enable = channels_[channel]->fec;
    redPayloadtype = channels_[channel]->fec_type;
    return 0;
  }
  WEBRTC_STUB(SetRTPKeepaliveStatus, (int channel, bool enable,
                                      unsigned char unknownPayloadType,
                                      int deltaTransmitTimeSeconds));
  WEBRTC_STUB(GetRTPKeepaliveStatus, (int channel, bool& enabled,
                                      unsigned char& unknownPayloadType,
                                      int& deltaTransmitTimeSeconds));
  WEBRTC_STUB(StartRTPDump, (int channel, const char* fileNameUTF8,
                             webrtc::RTPDirections direction));
  WEBRTC_STUB(StopRTPDump, (int channel, webrtc::RTPDirections direction));
  WEBRTC_STUB(RTPDumpIsActive, (int channel, webrtc::RTPDirections direction));
  WEBRTC_STUB(InsertExtraRTPPacket, (int channel, unsigned char payloadType,
                                     bool markerBit, const char* payloadData,
                                     unsigned short payloadSize));

  // webrtc::VoEVideoSync
  WEBRTC_STUB(GetPlayoutBufferSize, (int& bufferMs));
  WEBRTC_STUB(GetPlayoutTimestamp, (int channel, unsigned int& timestamp));
  WEBRTC_STUB(GetRtpRtcp, (int, webrtc::RtpRtcp*&));
  WEBRTC_STUB(SetInitTimestamp, (int channel, unsigned int timestamp));
  WEBRTC_STUB(SetInitSequenceNumber, (int channel, short sequenceNumber));
  WEBRTC_STUB(SetMinimumPlayoutDelay, (int channel, int delayMs));
  WEBRTC_STUB(GetDelayEstimate, (int channel, int& delayMs));
  WEBRTC_STUB(GetSoundcardBufferSize, (int& bufferMs));

  // webrtc::VoEVolumeControl
  WEBRTC_STUB(SetSpeakerVolume, (unsigned int));
  WEBRTC_STUB(GetSpeakerVolume, (unsigned int&));
  WEBRTC_STUB(SetSystemOutputMute, (bool));
  WEBRTC_STUB(GetSystemOutputMute, (bool&));
  WEBRTC_STUB(SetMicVolume, (unsigned int));
  WEBRTC_STUB(GetMicVolume, (unsigned int&));
  WEBRTC_STUB(SetInputMute, (int, bool));
  WEBRTC_STUB(GetInputMute, (int, bool&));
  WEBRTC_STUB(SetSystemInputMute, (bool));
  WEBRTC_STUB(GetSystemInputMute, (bool&));
  WEBRTC_STUB(GetSpeechInputLevel, (unsigned int&));
  WEBRTC_STUB(GetSpeechOutputLevel, (int, unsigned int&));
  WEBRTC_STUB(GetSpeechInputLevelFullRange, (unsigned int&));
  WEBRTC_STUB(GetSpeechOutputLevelFullRange, (int, unsigned int&));
  WEBRTC_STUB(SetChannelOutputVolumeScaling, (int, float));
  WEBRTC_STUB(GetChannelOutputVolumeScaling, (int, float&));
  WEBRTC_STUB(SetOutputVolumePan, (int, float, float));
  WEBRTC_STUB(GetOutputVolumePan, (int, float&, float&));

  // webrtc::VoEAudioProcessing
  WEBRTC_FUNC(SetNsStatus, (bool enable, webrtc::NsModes mode)) {
    ns_enabled_ = enable;
    ns_mode_ = mode;
    return 0;
  }
  WEBRTC_FUNC(GetNsStatus, (bool& enabled, webrtc::NsModes& mode)) {
    enabled = ns_enabled_;
    mode = ns_mode_;
    return 0;
  }
  WEBRTC_STUB(SetAgcStatus, (bool enable, webrtc::AgcModes mode));
  WEBRTC_STUB(GetAgcStatus, (bool& enabled, webrtc::AgcModes& mode));

  WEBRTC_FUNC(SetAgcConfig, (const webrtc::AgcConfig config)) {
    agc_config_ = config;
    return 0;
  }
  WEBRTC_FUNC(GetAgcConfig, (webrtc::AgcConfig& config)) {
    config = agc_config_;
    return 0;
  }
  WEBRTC_FUNC(SetEcStatus, (bool enable, webrtc::EcModes mode)) {
    ec_enabled_ = enable;
    ec_mode_ = mode;
    return 0;
  }
  WEBRTC_FUNC(GetEcStatus, (bool& enabled, webrtc::EcModes& mode)) {
    enabled = ec_enabled_;
    mode = ec_mode_;
    return 0;
  }
  WEBRTC_STUB(SetAecmMode, (webrtc::AecmModes mode, bool enableCNG));
  WEBRTC_STUB(GetAecmMode, (webrtc::AecmModes& mode, bool& enabledCNG));
  WEBRTC_STUB(SetRxNsStatus, (int channel, bool enable, webrtc::NsModes mode));
  WEBRTC_STUB(GetRxNsStatus, (int channel, bool& enabled,
                              webrtc::NsModes& mode));
  WEBRTC_STUB(SetRxAgcStatus, (int channel, bool enable,
                               webrtc::AgcModes mode));
  WEBRTC_STUB(GetRxAgcStatus, (int channel, bool& enabled,
                               webrtc::AgcModes& mode));
  WEBRTC_STUB(SetRxAgcConfig, (int channel, const webrtc::AgcConfig config));
  WEBRTC_STUB(GetRxAgcConfig, (int channel, webrtc::AgcConfig& config));

  WEBRTC_STUB(RegisterRxVadObserver, (int, webrtc::VoERxVadCallback&));
  WEBRTC_STUB(DeRegisterRxVadObserver, (int channel));
  WEBRTC_STUB(VoiceActivityIndicator, (int channel));
  WEBRTC_STUB(SetEcMetricsStatus, (bool enable));
  WEBRTC_STUB(GetEcMetricsStatus, (bool& enable));
  WEBRTC_STUB(GetEchoMetrics, (int& ERL, int& ERLE, int& RERL, int& A_NLP));
  WEBRTC_STUB(GetEcDelayMetrics, (int& delay_median, int& delay_std));

  WEBRTC_STUB(StartDebugRecording, (const char* fileNameUTF8));
  WEBRTC_STUB(StopDebugRecording, ());

  WEBRTC_STUB(SetTypingDetectionStatus, (bool enable));
  WEBRTC_STUB(GetTypingDetectionStatus, (bool& enabled));

  // webrtc::VoEExternalMedia
  WEBRTC_FUNC(RegisterExternalMediaProcessing,
              (int channel, webrtc::ProcessingTypes type,
               webrtc::VoEMediaProcess& processObject)) {
    media_processor_ = &processObject;
    return 0;
  }
  WEBRTC_FUNC(DeRegisterExternalMediaProcessing,
              (int channel, webrtc::ProcessingTypes type)) {
    media_processor_ = NULL;
    return 0;
  }
  WEBRTC_STUB(SetExternalRecordingStatus, (bool enable));
  WEBRTC_STUB(SetExternalPlayoutStatus, (bool enable));
  WEBRTC_STUB(ExternalRecordingInsertData,
              (const WebRtc_Word16 speechData10ms[], int lengthSamples,
               int samplingFreqHz, int current_delay_ms));
  WEBRTC_STUB(ExternalPlayoutGetData,
              (WebRtc_Word16 speechData10ms[], int samplingFreqHz,
               int current_delay_ms, int& lengthSamples));

 private:
  int GetNumDevices(int& num) {
#ifdef WIN32
    num = 1;
#else
    // On non-Windows platforms VE adds a special entry for the default device,
    // so if there is one physical device then there are two entries in the
    // list.
    num = 2;
#endif
    return 0;
  }

  int GetDeviceName(int i, char* name, char* guid) {
    const char *s;
#ifdef WIN32
    if (0 == i) {
      s = kFakeDeviceName;
    } else {
      return -1;
    }
#else
    // See comment above.
    if (0 == i) {
      s = kFakeDefaultDeviceName;
    } else if (1 == i) {
      s = kFakeDeviceName;
    } else {
      return -1;
    }
#endif
    strcpy(name, s);
    guid[0] = '\0';
    return 0;
  }

  bool inited_;
  int last_channel_;
  std::map<int, Channel*> channels_;
  bool fail_create_channel_;
  const cricket::AudioCodec* const* codecs_;
  int num_codecs_;
  bool ec_enabled_;
  bool ns_enabled_;
  webrtc::EcModes ec_mode_;
  webrtc::NsModes ns_mode_;
  webrtc::AgcConfig agc_config_;
  webrtc::VoiceEngineObserver* observer_;
  int playout_fail_channel_;
  int send_fail_channel_;
  bool fail_start_recording_microphone_;
  bool recording_microphone_;
  webrtc::VoEMediaProcess* media_processor_;
};

}  // namespace cricket

#endif  // TALK_SESSION_PHONE_FAKEWEBRTCVOICEENGINE_H_
