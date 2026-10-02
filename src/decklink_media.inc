// Included inside the SDK implementation's anonymous namespace.
struct OwnedPacket {
    VtrPonDeckLinkPacket info{};
    std::vector<uint8_t> video;
    std::vector<int32_t> audio;
};
struct InputState {
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<OwnedPacket> packets;
    OwnedPacket borrowed;
    BMDDisplayMode pendingMode = bmdModeUnknown;
    BMDPixelFormat pendingPixel = bmdFormat8BitYUV;
    BMDVideoInputFlags flags = bmdVideoInputFlagDefault;
    uint32_t field = 0, duration = 1001, scale = 30000;
    bool stopping = false;
};
struct InputSession {
    ComPtr<IDeckLinkInput> input;
    std::shared_ptr<InputState> state = std::make_shared<InputState>();
    bool stopped = false;
    void stop() {
        if (stopped) return; stopped = true;
        { std::lock_guard lock(state->mutex); state->stopping = true; }
        if (input) { input->StopStreams(); input->SetCallback(nullptr); input->DisableAudioInput(); input->DisableVideoInput(); }
    }
    ~InputSession() { stop(); }
};
class InputCallback final : public IDeckLinkInputCallback {
    std::atomic<ULONG> references{1};
    std::weak_ptr<InputState> state;
public:
    explicit InputCallback(const std::shared_ptr<InputState> &value) : state(value) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **out) override {
        if (!out) return E_POINTER; *out = nullptr;
        if (iid == IID_IUnknown || iid == IID_IDeckLinkInputCallback) { *out = static_cast<IDeckLinkInputCallback *>(this); AddRef(); return S_OK; }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --references; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE VideoInputFormatChanged(BMDVideoInputFormatChangedEvents,
        IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags detected) override {
        auto s = state.lock(); if (!s || !mode) return S_OK;
        BMDTimeValue duration = 0; BMDTimeScale scale = 0;
        if (FAILED(mode->GetFrameRate(&duration, &scale)) || duration <= 0 || scale <= 0 || scale > UINT32_MAX) return E_FAIL;
        std::lock_guard lock(s->mutex);
        s->pendingMode = mode->GetDisplayMode();
        s->pendingPixel = (detected & bmdDetectedVideoInputRGB444) ? bmdFormat8BitBGRA : bmdFormat8BitYUV;
        s->field = mode->GetFieldDominance(); s->duration = static_cast<uint32_t>(duration); s->scale = static_cast<uint32_t>(scale);
        s->wake.notify_one(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE VideoInputFrameArrived(IDeckLinkVideoInputFrame *frame, IDeckLinkAudioInputPacket *audio) override {
        auto s = state.lock(); if (!s) return S_OK;
        try {
            OwnedPacket packet; auto &p = packet.info; p.structSize = sizeof(p);
            { std::lock_guard lock(s->mutex); if (s->stopping) return S_OK; p.fieldDominance = s->field; p.frameDuration = s->duration; p.timeScale = s->scale; }
            if (frame) {
                p.noSignal = !!(frame->GetFlags() & bmdFrameHasNoInputSource);
                p.width = frame->GetWidth(); p.height = frame->GetHeight(); p.rowBytes = frame->GetRowBytes();
                p.pixelFormat = frame->GetPixelFormat() == bmdFormat8BitBGRA ? VtrPonDeckLinkBgra : VtrPonDeckLinkUyvy;
                frame->GetStreamTime(&p.videoTime100ns, &p.frameDuration100ns, 10000000);
                const uint64_t size = uint64_t(p.rowBytes) * p.height;
                if (!p.noSignal && p.width && p.width <= 4096 && p.height && p.height <= 2160 &&
                    p.rowBytes >= p.width * (p.pixelFormat == VtrPonDeckLinkBgra ? 4u : 2u) && size <= 64u * 1024u * 1024u) {
                    ComPtr<IDeckLinkVideoBuffer> buffer; void *bytes = nullptr; ULONGLONG available = 0;
                    if (SUCCEEDED(frame->QueryInterface(IID_IDeckLinkVideoBuffer, reinterpret_cast<void **>(buffer.GetAddressOf()))) &&
                        SUCCEEDED(buffer->StartAccess(bmdBufferAccessRead))) {
                        if (SUCCEEDED(buffer->GetBytes(&bytes)) && SUCCEEDED(buffer->GetSize(&available)) && bytes && available >= size) {
                            // End access even when the owned frame allocation fails.
                            try { packet.video.assign(static_cast<uint8_t *>(bytes), static_cast<uint8_t *>(bytes) + size); }
                            catch (...) { buffer->EndAccess(bmdBufferAccessRead); throw; }
                        }
                        buffer->EndAccess(bmdBufferAccessRead);
                    }
                    p.videoBytes = static_cast<uint32_t>(packet.video.size());
                }
            }
            if (audio) {
                const auto count = audio->GetSampleFrameCount(); void *bytes = nullptr;
                if (count > 0 && count <= 48000 && SUCCEEDED(audio->GetBytes(&bytes)) && bytes) {
                    packet.audio.assign(static_cast<int32_t *>(bytes), static_cast<int32_t *>(bytes) + size_t(count) * 2);
                    p.audioFrames = count; audio->GetPacketTime(&p.audioTime100ns, 10000000);
                }
            }
            std::lock_guard lock(s->mutex); if (s->stopping) return S_OK;
            if (s->packets.size() == 3) s->packets.pop_front();
            s->packets.push_back(std::move(packet)); s->wake.notify_one(); return S_OK;
        } catch (...) { return E_OUTOFMEMORY; }
    }
};
struct OutputState {
    ComPtr<IDeckLinkOutput> output;
    ComPtr<IDeckLinkConfiguration> groupConfig;
    ComPtr<IDeckLinkStatus> status;
    int64_t groupId = 0, previousGroup = 0, boardId = 0;
    bool internalReference = false;
    bool videoEnabled = false, audioEnabled = false;
    ComPtr<IDeckLinkVideoConversion> converter;
    int width = 0, height = 0, rowBytes = 0;
    BMDPixelFormat pixel = bmdFormat8BitBGRA;
    BMDTimeValue duration = 0, nextVideo = 0, nextAudio = 0;
    BMDTimeScale scale = 0;
    std::vector<uint8_t> latest;
    struct TimedVideo { int64_t time; std::vector<uint8_t> bytes; };
    struct TimedAudio { int64_t start; std::vector<int32_t> samples; };
    std::deque<TimedVideo> video;
    std::deque<TimedAudio> audio;
    int64_t epoch = 0, latency100ns = 0, latencySamples = 0;
    size_t audioSamples = 0;
    bool running = false;
    ~OutputState() {
        if (output) {
            if (running) { BMDTimeValue stop = 0; output->StopScheduledPlayback(0, &stop, scale); }
            if (audioEnabled) output->DisableAudioOutput();
            if (videoEnabled) output->DisableVideoOutput();
        }
        if (groupConfig) groupConfig->SetInt(bmdDeckLinkConfigPlaybackGroup, previousGroup);
    }
};
void closeMedia(Context *c) noexcept { if (c->inputState) std::static_pointer_cast<InputSession>(c->inputState)->stop(); c->inputState.reset(); c->outputState.reset(); }
template<class Fn> int32_t mediaCall(void *handle, char *error, uint32_t capacity, Fn &&fn) noexcept {
    const auto status = check(handle, error, capacity); if (status) return status;
    try { return fn(*static_cast<Context *>(handle)); }
    catch (...) { message(error, capacity, "DeckLink media allocation or backend operation failed."); return VtrPonDeckLinkBackendError; }
}
int32_t VTRPON_DECKLINK_CALL channels(void *handle, VtrPonDeckLinkChannel *out, uint32_t capacity,
    uint32_t *count, char *error, uint32_t errorCapacity) noexcept {
    if (count) *count = 0;
    return mediaCall(handle, error, errorCapacity, [&](Context &c) -> int32_t {
        if (!count || (!out && capacity) || !c.snapshot) return VtrPonDeckLinkInvalidArgument;
        std::vector<VtrPonDeckLinkChannel> records;
        for (uint32_t i = 0; i < c.devices.size(); ++i) {
            ComPtr<IDeckLinkProfileAttributes> attrs;
            if (FAILED(c.devices[i].As(&attrs))) continue;
            LONGLONG id = 0, sub = 0, support = 0;
            const bool stable = SUCCEEDED(attrs->GetInt(BMDDeckLinkPersistentID, &id));
            attrs->GetInt(BMDDeckLinkSubDeviceIndex, &sub); attrs->GetInt(BMDDeckLinkVideoIOSupport, &support);
            for (uint32_t direction : {1u, 2u}) {
                if (!(support & (direction == 1 ? bmdDeviceSupportsCapture : bmdDeviceSupportsPlayback))) continue;
                LONGLONG connections = 0;
                if (FAILED(attrs->GetInt(direction == 1 ? BMDDeckLinkVideoInputConnections : BMDDeckLinkVideoOutputConnections, &connections))) continue;
                for (uint32_t bit = 1; bit && bit <= 0x80; bit <<= 1) if (connections & bit) {
                    VtrPonDeckLinkChannel channel{}; channel.structSize = sizeof(channel); channel.deviceIndex = i;
                    channel.direction = direction; channel.connection = bit; channel.persistentId = id;
                    channel.subDeviceIndex = static_cast<uint32_t>(sub); channel.stableId = stable;
                    Bstr name; if (SUCCEEDED(c.devices[i]->GetDisplayName(&name.value))) utf8(name.value, channel.nameUtf8, sizeof(channel.nameUtf8));
                    records.push_back(channel);
                }
            }
        }
        return copy(records, out, capacity, count, error, errorCapacity);
    });
}
ComPtr<IDeckLink> selected(Context &c, const VtrPonDeckLinkStreamOptions *options, uint32_t direction) {
    if (!options || options->structSize != sizeof(*options) || !options->connection ||
        (options->connection & (options->connection - 1))) return {};
    for (auto &device : c.devices) {
        ComPtr<IDeckLinkProfileAttributes> a; LONGLONG id = 0, sub = 0, connections = 0;
        if (FAILED(device.As(&a)) || FAILED(a->GetInt(BMDDeckLinkPersistentID, &id))) continue;
        a->GetInt(BMDDeckLinkSubDeviceIndex, &sub);
        a->GetInt(direction == 1 ? BMDDeckLinkVideoInputConnections : BMDDeckLinkVideoOutputConnections, &connections);
        if (id == options->persistentId && sub == options->subDeviceIndex && (connections & options->connection)) return device;
    }
    return {};
}
int32_t VTRPON_DECKLINK_CALL openInput(void *handle, const VtrPonDeckLinkStreamOptions *options, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [&](Context &c) -> int32_t {
        if (c.inputState) return VtrPonDeckLinkInvalidArgument;
        auto device = selected(c, options, 1); if (!device) { message(error, cap, "Selected input channel is unavailable."); return VtrPonDeckLinkInvalidArgument; }
        auto session = std::make_shared<InputSession>(); auto s = session->state; ComPtr<IDeckLinkConfiguration> config; ComPtr<IDeckLinkProfileAttributes> attrs;
        if (FAILED(device.As(&session->input)) || FAILED(device.As(&config)) ||
            FAILED(config->SetInt(bmdDeckLinkConfigVideoInputConnection, options->connection))) { message(error, cap, "Input connection is unavailable or busy."); return VtrPonDeckLinkBackendError; }
        // Use audio embedded in the selected digital video connector. Fixed
        // audio connections need no configuration; analog uses the device setup.
        if (options->connection & (bmdVideoConnectionSDI | bmdVideoConnectionHDMI | bmdVideoConnectionOpticalSDI)) {
            LONGLONG audioConnection = 0;
            if (SUCCEEDED(config->GetInt(bmdDeckLinkConfigAudioInputConnection, &audioConnection)) &&
                audioConnection != bmdAudioConnectionEmbedded &&
                FAILED(config->SetInt(bmdDeckLinkConfigAudioInputConnection, bmdAudioConnectionEmbedded))) {
                message(error, cap, "Embedded audio input could not be selected."); return VtrPonDeckLinkBackendError;
            }
        }
        BOOL detect = FALSE; if (SUCCEEDED(device.As(&attrs))) attrs->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &detect);
        if (!options->modeId && detect) s->flags = bmdVideoInputEnableFormatDetection;
        ComPtr<IDeckLinkDisplayMode> mode;
        if (options->modeId) session->input->GetDisplayMode(static_cast<BMDDisplayMode>(options->modeId), &mode);
        else {
            ComPtr<IDeckLinkDisplayModeIterator> it; session->input->GetDisplayModeIterator(&it);
            if (it) for (;;) {
                ComPtr<IDeckLinkDisplayMode> candidate; if (it->Next(&candidate) != S_OK) break;
                BOOL supported = FALSE; BMDDisplayMode actual{};
                session->input->DoesSupportVideoMode(static_cast<BMDVideoConnection>(options->connection), candidate->GetDisplayMode(),
                    bmdFormat8BitYUV, bmdNoVideoInputConversion, bmdSupportedVideoModeDefault, &actual, &supported);
                if (supported && candidate->GetWidth() <= 4096 && candidate->GetHeight() <= 2160) { mode = candidate; break; }
            }
        }
        if (!mode) { message(error, cap, "No supported input format was found."); return VtrPonDeckLinkBackendError; }
        BMDTimeValue duration = 0; BMDTimeScale scale = 0; mode->GetFrameRate(&duration, &scale);
        s->field = mode->GetFieldDominance(); s->duration = static_cast<uint32_t>(duration); s->scale = static_cast<uint32_t>(scale);
        ComPtr<IDeckLinkInputCallback> callback; callback.Attach(new InputCallback(s));
        if (FAILED(session->input->SetCallback(callback.Get())) || FAILED(session->input->EnableVideoInput(mode->GetDisplayMode(), bmdFormat8BitYUV, s->flags)) ||
            FAILED(session->input->EnableAudioInput(bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger, 2)) || FAILED(session->input->StartStreams())) {
            message(error, cap, "DeckLink input could not start; check device profile, format and channel ownership."); return VtrPonDeckLinkBackendError;
        }
        c.inputState = session; return VtrPonDeckLinkOk;
    });
}
int32_t VTRPON_DECKLINK_CALL readInput(void *handle, VtrPonDeckLinkPacket *out, uint32_t timeout, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [&](Context &c) -> int32_t {
        if (!out || !c.inputState || timeout > 50) return VtrPonDeckLinkInvalidArgument;
        *out = {}; out->structSize = sizeof(*out);
        auto session = std::static_pointer_cast<InputSession>(c.inputState); auto s = session->state;
        std::unique_lock lock(s->mutex);
        s->wake.wait_for(lock, std::chrono::milliseconds(timeout), [&] { return !s->packets.empty() || s->pendingMode != bmdModeUnknown; });
        if (s->pendingMode != bmdModeUnknown) {
            const auto mode = s->pendingMode; const auto pixel = s->pendingPixel; s->pendingMode = bmdModeUnknown; s->packets.clear(); lock.unlock();
            if (FAILED(session->input->StopStreams()) || FAILED(session->input->EnableVideoInput(mode, pixel, s->flags)) ||
                FAILED(session->input->FlushStreams()) || FAILED(session->input->StartStreams())) {
                message(error, cap, "Detected input format could not be applied."); return VtrPonDeckLinkBackendError;
            }
            return VtrPonDeckLinkOk;
        }
        if (s->packets.empty()) return VtrPonDeckLinkOk;
        s->borrowed = std::move(s->packets.front()); s->packets.pop_front(); *out = s->borrowed.info;
        out->video = s->borrowed.video.data(); out->audio = s->borrowed.audio.data(); return VtrPonDeckLinkOk;
    });
}
int32_t VTRPON_DECKLINK_CALL closeInput(void *handle, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [](Context &c) -> int32_t { if (c.inputState) std::static_pointer_cast<InputSession>(c.inputState)->stop(); c.inputState.reset(); return VtrPonDeckLinkOk; });
}
int32_t pump(OutputState &s) {
    if (s.groupId && s.running && !s.internalReference) {
        BOOL locked = FALSE;
        if (!s.status || FAILED(s.status->GetFlag(bmdDeckLinkStatusReferenceSignalLocked, &locked)) || !locked)
            return VtrPonDeckLinkBackendError;
    }
    unsigned buffered = 0;
    if (FAILED(s.output->GetBufferedVideoFrameCount(&buffered))) return VtrPonDeckLinkBackendError;
    BMDTimeValue clock = 0; double speed = 0;
    if (s.running && SUCCEEDED(s.output->GetScheduledStreamTime(s.scale, &clock, &speed)))
        s.nextVideo = std::max(s.nextVideo, (clock / s.duration + 1) * s.duration);
    while (buffered++ < 3) {
        const auto elapsed = vtrpon::decklink::frameTime100ns(s.nextVideo / s.duration, s.duration, s.scale);
        if (!elapsed || s.epoch > INT64_MAX - *elapsed) return VtrPonDeckLinkBackendError;
        const auto cutoff = s.epoch + *elapsed - s.latency100ns;
        while (!s.video.empty() && s.video.front().time <= cutoff) {
            s.latest = std::move(s.video.front().bytes); s.video.pop_front();
        }
        ComPtr<IDeckLinkMutableVideoFrame> source, frame;
        if (FAILED(s.output->CreateVideoFrame(s.width, s.height, s.width * 4, bmdFormat8BitBGRA, bmdFrameFlagDefault, &source))) return VtrPonDeckLinkBackendError;
        ComPtr<IDeckLinkVideoBuffer> buffer; void *bytes = nullptr;
        if (FAILED(source.As(&buffer)) || FAILED(buffer->StartAccess(bmdBufferAccessWrite))) return VtrPonDeckLinkBackendError;
        const auto hr = buffer->GetBytes(&bytes);
        if (SUCCEEDED(hr) && bytes) std::memcpy(bytes, s.latest.data(), s.latest.size());
        buffer->EndAccess(bmdBufferAccessWrite); if (FAILED(hr) || !bytes) return VtrPonDeckLinkBackendError;
        frame = source;
        if (s.pixel != bmdFormat8BitBGRA) {
            if (FAILED(s.output->CreateVideoFrame(s.width, s.height, s.rowBytes, s.pixel, bmdFrameFlagDefault, &frame)) ||
                FAILED(s.converter->ConvertFrame(source.Get(), frame.Get()))) return VtrPonDeckLinkBackendError;
        }
        if (FAILED(s.output->ScheduleVideoFrame(frame.Get(), s.nextVideo, s.duration, s.scale))) return VtrPonDeckLinkBackendError;
        s.nextVideo += s.duration;
    }
    if (FAILED(s.output->GetBufferedAudioSampleFrameCount(&buffered))) return VtrPonDeckLinkBackendError;
    if (s.running && SUCCEEDED(s.output->GetScheduledStreamTime(48000, &clock, &speed))) s.nextAudio = std::max(s.nextAudio, clock);
    const auto target = static_cast<unsigned>((48000 * s.duration * 3 + s.scale - 1) / s.scale);
    if (buffered < target) {
        const unsigned count = target - buffered; std::vector<int32_t> samples(size_t(count) * 2, 0);
        for (const auto &clip : s.audio) {
            const auto begin = std::max(s.nextAudio, clip.start);
            const auto end = std::min(s.nextAudio + count, clip.start + static_cast<int64_t>(clip.samples.size() / 2));
            if (begin < end) std::copy_n(clip.samples.data() + (begin - clip.start) * 2,
                size_t(end - begin) * 2, samples.data() + (begin - s.nextAudio) * 2);
        }
        unsigned written = 0;
        if (FAILED(s.output->ScheduleAudioSamples(samples.data(), count, s.nextAudio, 48000, &written)) || written > count) return VtrPonDeckLinkBackendError;
        s.nextAudio += written;
        while (!s.audio.empty() && s.audio.front().start + static_cast<int64_t>(s.audio.front().samples.size() / 2) <= s.nextAudio) {
            s.audioSamples -= s.audio.front().samples.size(); s.audio.pop_front();
        }
    }
    return VtrPonDeckLinkOk;
}
int32_t openOutputImpl(void *handle, const VtrPonDeckLinkStreamOptions *options, int64_t groupId, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [&](Context &c) -> int32_t {
        if (c.outputState || !options || options->structSize != sizeof(*options) || !options->modeId || options->outputStartTime100ns <= 0) return VtrPonDeckLinkInvalidArgument;
        auto device = selected(c, options, 2); if (!device) return VtrPonDeckLinkInvalidArgument;
        auto s = std::make_shared<OutputState>(); ComPtr<IDeckLinkConfiguration> config; ComPtr<IDeckLinkDisplayMode> mode;
        if (FAILED(device.As(&s->output)) || FAILED(device.As(&config)) ||
            FAILED(config->SetInt(bmdDeckLinkConfigVideoOutputConnection, options->connection)) ||
            FAILED(s->output->GetDisplayMode(static_cast<BMDDisplayMode>(options->modeId), &mode))) return VtrPonDeckLinkBackendError;
        s->width = mode->GetWidth(); s->height = mode->GetHeight();
        if (groupId) {
            ComPtr<IDeckLinkProfileAttributes> attributes; BOOL supported = FALSE, locked = FALSE;
            Bstr model; device->GetModelName(&model.value);
            s->internalReference = model.value && std::wcsstr(model.value, L"DeckLink 8K Pro");
            if (FAILED(device.As(&attributes)) || FAILED(attributes->GetFlag(BMDDeckLinkSupportsSynchronizeToPlaybackGroup, &supported)) || !supported) {
                message(error, cap, "This device does not support synchronized playback groups."); return VTRPON_DECKLINK_SYNC_UNAVAILABLE;
            }
            attributes->GetInt(BMDDeckLinkDeviceGroupID, &s->boardId);
            device.As(&s->status);
            if (!s->internalReference && (FAILED(device.As(&s->status)) ||
                FAILED(s->status->GetFlag(bmdDeckLinkStatusReferenceSignalLocked, &locked)) || !locked)) {
                message(error, cap, "Fill/Key synchronization requires a locked common reference input."); return VTRPON_DECKLINK_SYNC_UNAVAILABLE;
            }
            if (FAILED(config->GetInt(bmdDeckLinkConfigPlaybackGroup, &s->previousGroup)) ||
                FAILED(config->SetInt(bmdDeckLinkConfigPlaybackGroup, groupId))) return VtrPonDeckLinkBackendError;
            s->groupConfig = config; s->groupId = groupId;
        }
        if (s->width <= 0 || s->width > 4096 || s->height <= 0 || s->height > 2160 || FAILED(mode->GetFrameRate(&s->duration, &s->scale)) || s->duration <= 0 || s->scale <= 0) return VtrPonDeckLinkInvalidArgument;
        if (s->duration > 1000000000 || s->scale > 1000000000 || s->scale < s->duration * 10 || s->scale > s->duration * 240) return VtrPonDeckLinkInvalidArgument;
        BOOL supported = FALSE; BMDDisplayMode actual{};
        auto supports = [&](BMDPixelFormat pixel) { return SUCCEEDED(s->output->DoesSupportVideoMode(static_cast<BMDVideoConnection>(options->connection),
            mode->GetDisplayMode(), pixel, bmdNoVideoOutputConversion, bmdSupportedVideoModeDefault, &actual, &supported)) && supported && actual == mode->GetDisplayMode(); };
        if (!supports(s->pixel)) {
            s->pixel = bmdFormat8BitYUV;
            if (!supports(s->pixel) || FAILED(CoCreateInstance(CLSID_CDeckLinkVideoConversion, nullptr, CLSCTX_INPROC_SERVER,
                IID_IDeckLinkVideoConversion, reinterpret_cast<void **>(s->converter.GetAddressOf())))) {
                message(error, cap, "Selected output mode or pixel format is unsupported."); return VtrPonDeckLinkBackendError;
            }
        }
        if (FAILED(s->output->RowBytesForPixelFormat(s->pixel, s->width, &s->rowBytes)) ||
            FAILED(s->output->EnableVideoOutput(mode->GetDisplayMode(), groupId ? bmdVideoOutputSynchronizeToPlaybackGroup : bmdVideoOutputFlagDefault))) return VtrPonDeckLinkBackendError;
        s->videoEnabled = true;
        if (FAILED(s->output->EnableAudioOutput(bmdAudioSampleRate48kHz, bmdAudioSampleType32bitInteger, 2, bmdAudioOutputStreamTimestamped))) return VtrPonDeckLinkBackendError;
        s->audioEnabled = true;
        s->epoch = options->outputStartTime100ns;
        s->latency100ns = *vtrpon::decklink::frameTime100ns(3, s->duration, s->scale);
        s->latencySamples = (s->duration * 3 * 48000 + s->scale - 1) / s->scale;
        s->latest.resize(size_t(s->width) * s->height * 4, 0);
        for (size_t i = 3; i < s->latest.size(); i += 4) s->latest[i] = 255;
        // Audio is scheduled explicitly on this owner worker. BeginAudioPreroll
        // polls an audio callback, which this implementation does not register.
        if (pump(*s) != VtrPonDeckLinkOk) return VtrPonDeckLinkBackendError;
        if (!groupId) {
            if (FAILED(s->output->StartScheduledPlayback(0, s->scale, 1.0))) return VtrPonDeckLinkBackendError;
            s->running = true;
        }
        c.outputState = s; return VtrPonDeckLinkOk;
    });
}
int32_t VTRPON_DECKLINK_CALL openOutput(void *handle, const VtrPonDeckLinkStreamOptions *options, char *error, uint32_t cap) noexcept {
    return openOutputImpl(handle, options, 0, error, cap);
}
int32_t VTRPON_DECKLINK_CALL openGroupedOutput(void *handle, const VtrPonDeckLinkStreamOptions *options, int64_t groupId, char *error, uint32_t cap) noexcept {
    if (groupId <= 0) return VtrPonDeckLinkInvalidArgument;
    return openOutputImpl(handle, options, groupId, error, cap);
}
int32_t VTRPON_DECKLINK_CALL startGroup(void *const *handles, uint32_t count, char *error, uint32_t cap) noexcept {
    if (!handles || count < 2 || count > 3) return VtrPonDeckLinkInvalidArgument;
    std::shared_ptr<OutputState> leader; bool internalReference = true;
    for (uint32_t i = 0; i < count; ++i) {
        const auto result = check(handles[i], error, cap); if (result) return result;
        auto s = std::static_pointer_cast<OutputState>(static_cast<Context *>(handles[i])->outputState);
        if (!s || !s->groupId || s->running) return VtrPonDeckLinkInvalidArgument;
        for (uint32_t j = 0; j < i; ++j) if (handles[j] == handles[i]) return VtrPonDeckLinkInvalidArgument;
        if (!leader) leader = s;
        if (s->groupId != leader->groupId || s->duration != leader->duration || s->scale != leader->scale || s->epoch != leader->epoch || s->width != leader->width || s->height != leader->height)
            return VtrPonDeckLinkInvalidArgument;
        internalReference = internalReference && s->internalReference && s->boardId && s->boardId == leader->boardId;
    }
    // The 8K Pro exception applies only to subdevices of the same card.
    for (uint32_t i = 0; i < count; ++i) {
        auto s = std::static_pointer_cast<OutputState>(static_cast<Context *>(handles[i])->outputState);
        BOOL locked = FALSE;
        if (!internalReference && (!s->status || FAILED(s->status->GetFlag(bmdDeckLinkStatusReferenceSignalLocked, &locked)) || !locked)) {
            message(error, cap, "Fill/Key synchronization requires a locked common reference input."); return VTRPON_DECKLINK_SYNC_UNAVAILABLE;
        }
        s->internalReference = internalReference;
    }
    if (FAILED(leader->output->StartScheduledPlayback(0, leader->scale, 1.0))) return VtrPonDeckLinkBackendError;
    for (uint32_t i = 0; i < count; ++i) std::static_pointer_cast<OutputState>(static_cast<Context *>(handles[i])->outputState)->running = true;
    return VtrPonDeckLinkOk;
}
int32_t VTRPON_DECKLINK_CALL sendVideo(void *handle, const uint8_t *bytes, uint32_t width, uint32_t height, int64_t time, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [&](Context &c) -> int32_t {
        if (!c.outputState || !bytes || time <= 0) return VtrPonDeckLinkInvalidArgument;
        auto s = std::static_pointer_cast<OutputState>(c.outputState);
        if (width != static_cast<uint32_t>(s->width) || height != static_cast<uint32_t>(s->height)) return VtrPonDeckLinkInvalidArgument;
        if (!s->video.empty() && time <= s->video.back().time) return VtrPonDeckLinkInvalidArgument;
        OutputState::TimedVideo frame{time, std::vector<uint8_t>(bytes, bytes + s->latest.size())};
        if (s->video.size() == 8) s->video.pop_front();
        s->video.push_back(std::move(frame)); return VtrPonDeckLinkOk;
    });
}
int32_t VTRPON_DECKLINK_CALL sendAudio(void *handle, const float *samples, uint32_t frames, int64_t time, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [&](Context &c) -> int32_t {
        if (!c.outputState || !samples || frames > 48000 || time <= 0) return VtrPonDeckLinkInvalidArgument;
        auto s = std::static_pointer_cast<OutputState>(c.outputState);
        for (size_t i = 0; i < size_t(frames) * 2; ++i) if (!std::isfinite(samples[i])) return VtrPonDeckLinkInvalidArgument;
        const auto relative = time - s->epoch;
        const auto offset = vtrpon::decklink::audioTime48k(relative);
        if (!offset || *offset > INT64_MAX - s->latencySamples) return VtrPonDeckLinkInvalidArgument;
        const auto start = *offset + s->latencySamples;
        OutputState::TimedAudio clip{start, {}}; clip.samples.reserve(size_t(frames) * 2);
        for (size_t i = 0; i < size_t(frames) * 2; ++i)
            clip.samples.push_back(static_cast<int32_t>(std::clamp(double(samples[i]), -1.0, 1.0) * 2147483647.0));
        if (s->audioSamples + clip.samples.size() > 96000) { s->audio.clear(); s->audioSamples = 0; }
        s->audioSamples += clip.samples.size(); s->audio.push_back(std::move(clip));
        return VtrPonDeckLinkOk;
    });
}
int32_t VTRPON_DECKLINK_CALL pumpOutput(void *handle, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [&](Context &c) -> int32_t {
        if (!c.outputState) return VtrPonDeckLinkInvalidArgument;
        const auto status = pump(*std::static_pointer_cast<OutputState>(c.outputState));
        if (status) message(error, cap, "DeckLink scheduled output failed; the device may have disconnected."); return status;
    });
}
int32_t VTRPON_DECKLINK_CALL closeOutput(void *handle, char *error, uint32_t cap) noexcept {
    return mediaCall(handle, error, cap, [](Context &c) -> int32_t { c.outputState.reset(); return VtrPonDeckLinkOk; });
}
const VtrPonDeckLinkMediaApi mediaApi{1, sizeof(VtrPonDeckLinkMediaApi), 0, 0, "vtr-pon-decklink-addon",
    channels, openInput, readInput, closeInput, openOutput, sendVideo, sendAudio, pumpOutput, closeOutput};
const VtrPonDeckLinkOutputApi outputApi{1, sizeof(VtrPonDeckLinkOutputApi), 0, 0, "vtr-pon-decklink-addon", openGroupedOutput, startGroup};
