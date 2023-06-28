#include "peerconnection.h"

#include <stdexcept>
#include <iostream>
#include <chrono>

#include <api/create_peerconnection_factory.h>
#include <rtc_base/ssl_adapter.h>

#include <api/video_codecs/builtin_video_decoder_factory.h>
#include <api/video_codecs/builtin_video_encoder_factory.h>
#include <api/audio_codecs/builtin_audio_decoder_factory.h>
#include <api/audio_codecs/builtin_audio_encoder_factory.h>
#include <api/jsep.h>
#include <api/stats/rtcstats_objects.h>
#include <rtc_base/thread.h>

#include <pc/session_description.h>

rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerconnectionMgr::_pcf = nullptr;
std::unique_ptr<rtc::Thread> PeerconnectionMgr::_signaling_th = nullptr;

rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> PeerconnectionMgr::get_pcf()
{
  if(_pcf != nullptr) return _pcf;

  rtc::InitRandom((int)rtc::Time());
  rtc::InitializeSSL();

  _signaling_th = rtc::Thread::Create();
  _signaling_th->SetName("WebRTCSignalingThread", nullptr);
  _signaling_th->Start();
  
  _pcf = webrtc::CreatePeerConnectionFactory(nullptr, nullptr, _signaling_th.get(), nullptr,
					     webrtc::CreateBuiltinAudioEncoderFactory(),
					     webrtc::CreateBuiltinAudioDecoderFactory(),
					     webrtc::CreateBuiltinVideoEncoderFactory(),
					     webrtc::CreateBuiltinVideoDecoderFactory(),
					     nullptr, nullptr);

  webrtc::PeerConnectionFactoryInterface::Options options;
  options.crypto_options.srtp.enable_gcm_crypto_suites = true;
  _pcf->SetOptions(options);
  
  return _pcf;
}

void PeerconnectionMgr::clean()
{
  _pcf = nullptr;
  rtc::CleanupSSL();
}

PeerconnectionMgr::PeerconnectionMgr() : _pc{nullptr}, _me(this)
{
  AddRef();
}

PeerconnectionMgr::~PeerconnectionMgr()
{
  Release();
}

void PeerconnectionMgr::start()
{
  std::cout << "Start peerconnection" << "\n";
  
  auto pcf = get_pcf();

  webrtc::PeerConnectionDependencies deps(this);
  webrtc::PeerConnectionInterface::RTCConfiguration config(webrtc::PeerConnectionInterface::RTCConfigurationType::kAggressive);

  config.set_cpu_adaptation(true);
  config.combined_audio_video_bwe.emplace(true);
  config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;

  auto res = pcf->CreatePeerConnectionOrError(config, std::move(deps));

  if(!res.ok()) {
    std::cerr << "Can't create peerconnection : " << res.error().message() << std::endl;
    throw std::runtime_error("Could not create peerconection");
  }

  _pc = res.value();

  _stats_th_running = false;
  stats.erase(stats.begin(), stats.end());
  _count = 0;
  _prev_ts = 0.;
  _prev_bytes = 0.;
  _key_frame = 0;
  _frames = 0;

  _file_bitstream.open("bitstream.264", std::ios::binary);
  
  webrtc::PeerConnectionInterface::RTCOfferAnswerOptions oa;
  oa.offer_to_receive_video = true;
  oa.offer_to_receive_audio = false;

  _signaling_th->BlockingCall([this, oa]() {
    std::cout << "creating offer" << "\n";
    _pc->CreateOffer(this, oa);
  });
}

void PeerconnectionMgr::OnSuccess(webrtc::SessionDescriptionInterface* desc)
{
  std::cout << "On create offer success" << std::endl;

  _pc->SetLocalDescription(std::unique_ptr<webrtc::SessionDescriptionInterface>(desc), _me);
  
}

void PeerconnectionMgr::OnFailure(webrtc::RTCError error)
{
  std::cout << "On create offer failure" << "\n";
}

void PeerconnectionMgr::OnSetLocalDescriptionComplete(webrtc::RTCError error)
{
  if(error.ok()) {
    std::cout << "On set local desc success" << "\n";
    
    std::string sdp;
    auto desc = _pc->local_description();
    desc->ToString(&sdp);

    if(onlocaldesc) onlocaldesc(sdp);
  }
  else {
    std::cerr << "On set local desc failure" << "\n";
  }
}

void PeerconnectionMgr::OnSetRemoteDescriptionComplete(webrtc::RTCError error)
{
  if(error.ok()) {
    std::cout << "On set remote desc success" << std::endl;
  }
  else {
    std::cerr << "On set remote desc failure" << std::endl;
  }
}

void PeerconnectionMgr::stop()
{
  std::cout << "PeerConnection::Stop" << "\n";
  _stats_th_running = false;
  if(_stats_th.joinable()) _stats_th.join();

  _file_bitstream.close();
  
  _pc->Close();
  _pc = nullptr;

  std::cout << "Received transformable frame : " << _frames << "\n";
}

void PeerconnectionMgr::set_remote_description(const std::string &sdp)
{
  std::cout << "Set remote desc" << std::endl;
  auto desc = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp);

  if(!desc) {
    std::cerr << "Error parsing remote sdp" << "\n";
    throw std::runtime_error("Could not set remote description");
  }

  _pc->SetRemoteDescription(std::move(desc), _me);
}

void PeerconnectionMgr::OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
{
  std::cout << "PeerconnectionMgr::OnStatsDelivered" << "\n";
  
  RTCStats rtc_stats;

  auto inbound_stats = report->GetStatsOfType<webrtc::RTCInboundRTPStreamStats>();

  std::cout << inbound_stats.size() << "\n";
  
  for(const auto& s : inbound_stats) {
    if(*s->kind == webrtc::RTCMediaStreamTrackKind::kVideo) {
      auto ts = s->timestamp();
      auto ts_ms = ts.ms();
      auto delta = ts_ms - _prev_ts;
      auto bytes = *s->bytes_received - _prev_bytes;
      
      rtc_stats.x = _count;
      rtc_stats.link = link;
      rtc_stats.bitrate = static_cast<int>(8. * bytes / delta);
      rtc_stats.fps = s->frames_per_second.ValueOrDefault(0.);
      rtc_stats.frame_dropped = s->frames_dropped.ValueOrDefault(0.);
      rtc_stats.frame_decoded = s->frames_decoded.ValueOrDefault(0.);
      rtc_stats.frame_key_decoded = s->key_frames_decoded.ValueOrDefault(0.);

      _prev_ts = ts_ms;
      _prev_bytes = *s->bytes_received;
    }
  }
  
  stats.push_back(std::move(rtc_stats));
}

void PeerconnectionMgr::Transform(std::unique_ptr<webrtc::TransformableFrameInterface> transformable_frame)
{
  auto ssrc = transformable_frame->GetSsrc();

  if(auto it = _callbacks.find(ssrc); it != _callbacks.end()) {
    auto video_frame = static_cast<webrtc::TransformableVideoFrameInterface*>(transformable_frame.get());
    if(video_frame->IsKeyFrame()) {
      std::cout << "Received new key frame: " << ++_key_frame << "\n";
    }

    ++_frames;

    if(_file_bitstream.is_open()) {
      _file_bitstream.write((const char*)video_frame->GetData().data(), video_frame->GetData().size());
    }
    
    it->second->OnTransformedFrame(std::move(transformable_frame));
  }
}

void PeerconnectionMgr::RegisterTransformedFrameSinkCallback(rtc::scoped_refptr<webrtc::TransformedFrameCallback> callback, uint32_t ssrc)
{
  _callbacks[ssrc] = callback;
}

void PeerconnectionMgr::UnregisterTransformedFrameSinkCallback(uint32_t ssrc)
{
  _callbacks.erase(ssrc);
}

void PeerconnectionMgr::OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) 
{
  switch(new_state) {
  case webrtc::PeerConnectionInterface::SignalingState::kClosed:
    std::cout << "pc on closed" << std::endl;
    break;
  case webrtc::PeerConnectionInterface::SignalingState::kStable:
    std::cout << "pc on stable" << "\n";
    break;
  default:
    break;
  }
}

void PeerconnectionMgr::OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) 
{}

void PeerconnectionMgr::OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) 
{}

void PeerconnectionMgr::OnAddTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver, const std::vector<rtc::scoped_refptr<webrtc::MediaStreamInterface>>& streams) 
{}

void PeerconnectionMgr::OnTrack(rtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) 
{
  std::cout << "PeerconnectionMgr::OnTrack" << "\n";
  
  _stats_th = std::thread([this, transceiver]() {
    _stats_th_running = true;
    _count = 0;
    
    while(_stats_th_running) {
      _pc->GetStats(transceiver->receiver(), _me);
      std::this_thread::sleep_for(std::chrono::seconds(1));
      ++_count;
    }
  });

  transceiver->receiver()->SetDepacketizerToDecoderFrameTransformer(_me);

  if(video_sink) {
    auto track = static_cast<webrtc::VideoTrackInterface*>(transceiver->receiver()->track().get());
    track->AddOrUpdateSink(video_sink, rtc::VideoSinkWants{});
  }
}

void PeerconnectionMgr::OnRemoveTrack(rtc::scoped_refptr<webrtc::RtpReceiverInterface> receiver) 
{}

void PeerconnectionMgr::OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> channel)  
{}

void PeerconnectionMgr::OnRenegotiationNeeded() 
{}

void PeerconnectionMgr::OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) 
{}

void PeerconnectionMgr::OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) 
{}

void PeerconnectionMgr::OnIceCandidate(const webrtc::IceCandidateInterface* candidate) 
{}

void PeerconnectionMgr::OnIceConnectionReceivingChange(bool receiving) 
{}

