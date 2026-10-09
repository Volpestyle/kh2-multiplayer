// Included only by the separate internal clip target, after canonical helpers.
namespace owned_clip {
struct Handle {
    HANDLE value = nullptr;
    explicit Handle(HANDLE h = nullptr) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};

std::uint64_t NowNs() {
    LARGE_INTEGER counter{}, frequency{};
    if (!QueryPerformanceCounter(&counter) || !QueryPerformanceFrequency(&frequency) ||
        frequency.QuadPart <= 0 || frequency.QuadPart > 1000000000LL || counter.QuadPart < 0)
        throw std::runtime_error("Monotonic clock unavailable");
    const auto n = static_cast<std::uint64_t>(counter.QuadPart);
    const auto f = static_cast<std::uint64_t>(frequency.QuadPart);
    return (n / f) * 1000000000ULL + (n % f) * 1000000000ULL / f;
}

struct Bound {
    std::uint64_t end;
    HANDLE cancel;
    void Check(std::uint64_t reserve = 2000000000ULL) const {
        const DWORD state = WaitForSingleObject(cancel, 0);
        if (state != WAIT_TIMEOUT) throw std::runtime_error("Clip cancellation or cancellation handle failure");
        const auto now = NowNs();
        if (now >= end || reserve >= end - now) throw std::runtime_error("Clip deadline/cleanup reserve exhausted");
    }
};

std::string HashBytes(const std::string& bytes) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider unavailable");
    BCRYPT_HASH_HANDLE hash = nullptr;
    const auto close = [&]() { if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg, 0); };
    std::array<unsigned char, 32> digest{};
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        close(); throw std::runtime_error("SHA256 failed");
    }
    close(); std::ostringstream out;
    for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b);
    return out.str();
}

std::string HashFile(const std::filesystem::path& path, const Bound& bound) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("SHA256 provider unavailable");
    BCRYPT_HASH_HANDLE hash = nullptr;
    const auto close = [&]() { if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg, 0); };
    try {
        if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) < 0) throw std::runtime_error("SHA256 create failed");
        std::ifstream in(path, std::ios::binary);
        if (!in) throw std::runtime_error("Cannot hash media product/image");
        std::array<char, 65536> block{};
        while (in) {
            bound.Check(); in.read(block.data(), static_cast<std::streamsize>(block.size()));
            const auto count = in.gcount();
            if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(block.data()), static_cast<ULONG>(count), 0) < 0)
                throw std::runtime_error("SHA256 update failed");
        }
        if (!in.eof()) throw std::runtime_error("Media product/image read failed");
        std::array<unsigned char, 32> digest{};
        if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) throw std::runtime_error("SHA256 finish failed");
        close(); hash = nullptr; alg = nullptr;
        std::ostringstream out;
        for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b);
        return out.str();
    } catch (...) { close(); throw; }
}

std::wstring Quote(const std::wstring& arg) {
    std::wstring out = L"\""; std::size_t slashes = 0;
    for (wchar_t c : arg) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'\"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
        else { out.append(slashes, L'\\'); out += c; }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\'); out += L'\"'; return out;
}

std::filesystem::path ImagePath(HANDLE process) {
    std::array<wchar_t, 32768> image{}; DWORD length = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(process, 0, image.data(), &length)) throw std::runtime_error("Process image query failed");
    return std::filesystem::canonical(image.data());
}

void Write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data; out.flush(); if (!out) throw std::runtime_error("Clip receipt write failed");
}

struct ChildResult { std::string json; };

ChildResult Child(const std::vector<std::string>& argv, const std::string& imageHash,
                  const std::filesystem::path& evidence, const std::string& stem,
                  const std::string& role, HANDLE job, const Bound& bound) {
    bound.Check();
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle stdoutFile(CreateFileW((evidence / (stem + "_" + role + ".stdout.log")).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                  &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle stderrFile(CreateFileW((evidence / (stem + "_" + role + ".stderr.log")).c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                  &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    if (!stdoutFile || !stderrFile || !input) throw std::runtime_error("Cannot create fresh child logs");
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = stdoutFile.value; startup.hStdError = stderrFile.value; startup.hStdInput = input.value;
    std::wstring command;
    for (const auto& a : argv) { if (!command.empty()) command += L' '; command += Quote(std::filesystem::path(a).wstring()); }
    PROCESS_INFORMATION info{};
    const auto executable = std::filesystem::path(argv.front());
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, RepoRoot().c_str(), &startup, &info))
        throw std::runtime_error("Media child CreateProcess failed " + std::to_string(GetLastError()));
    Handle process(info.hProcess), thread(info.hThread);
    const auto started = NowNs();
    bool assigned = false;
    try {
        assigned = AssignProcessToJobObject(job, process.value) != 0;
        if (!assigned) throw std::runtime_error("Cannot contain media child before resume");
        const auto creation = ProcessCreationTime(process.value);
        if (creation == 0 || !std::filesystem::equivalent(ImagePath(process.value), executable))
            throw std::runtime_error("Media child identity refused before resume");
        std::ostringstream identity;
        identity << "{\"pid\":" << info.dwProcessId << ",\"creationTicks\":" << creation
                 << ",\"parentPid\":" << GetCurrentProcessId() << ",\"imagePath\":" << JsonString(executable.string())
                 << ",\"imageSha256\":" << JsonString(imageHash) << ",\"argv\":" << JsonStringArray(argv) << "}";
        // Persist exact identity before any child executes, including failure/parent cancellation.
        Write(evidence / (stem + "_" + role + ".identity.json"), identity.str());
        if (ResumeThread(thread.value) != 1) throw std::runtime_error("Media child resume refused");
        for (;;) {
            bound.Check(); const DWORD state = WaitForSingleObject(process.value, 20);
            if (state == WAIT_OBJECT_0) break;
            if (state != WAIT_TIMEOUT) throw std::runtime_error("Media child wait failed");
        }
        const auto finished = NowNs(); DWORD code = 0;
        if (!GetExitCodeProcess(process.value, &code) || code != 0) throw std::runtime_error("Media child nonzero exit " + std::to_string(code));
        JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        // A signaled process handle can precede asynchronous job accounting.
        // Wait a bounded 100ms for zero; never accept a surviving descendant.
        const auto quiescentEnd = NowNs() + 100000000ULL;
        for (;;) {
            bound.Check();
            if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr))
                throw std::runtime_error("Media child job census failed");
            if (accounting.ActiveProcesses == 0) break;
            if (NowNs() >= quiescentEnd) throw std::runtime_error("Media encoder/verifier descendant remains");
            Sleep(10);
        }
        std::ostringstream receipt;
        receipt << "{\"identity\":" << identity.str() << ",\"startedNs\":" << started << ",\"finishedNs\":" << finished
                << ",\"exitCode\":0,\"exited\":true,\"identityAbsent\":true,\"absenceCheckedNs\":" << NowNs() << "}";
        Write(evidence / (stem + "_" + role + ".closed.json"), receipt.str());
        return {receipt.str()};
    } catch (...) {
        // No broad process discovery/kill: job contains only this helper's suspended-admitted children.
        if (assigned) TerminateJobObject(job, 125); else TerminateProcess(process.value, 125);
        const auto now = NowNs(); const auto ms = now < bound.end ? (bound.end - now) / 1000000ULL : 0;
        WaitForSingleObject(process.value, static_cast<DWORD>((std::min)(ms, 2000ULL)));
        throw;
    }
}

std::string OwnedRecord(DWORD pid, std::uint64_t creation) {
    std::ifstream in(RigDir() / "owned.txt", std::ios::binary); std::string line, selected; int count = 0;
    while (std::getline(in, line)) {
        std::istringstream row(line); DWORD p = 0; std::uint64_t c = 0; std::string extra;
        if (!(row >> p >> c) || (row >> extra)) throw std::runtime_error("Malformed native ownership registry");
        if (p == pid) { if (c != creation) throw std::runtime_error("Native owned creation differs"); selected = line + "\n"; ++count; }
    }
    if (count != 1) throw std::runtime_error("One original package owned record required");
    return selected;
}

CaptureCompletion Capture(kh2coop::CaptureChannel* channel, const std::wstring& output,
                          std::uint32_t interval, const Bound& bound) {
    bound.Check();
    if (output.size() >= std::size(channel->output) || channel->requestSeq != channel->doneSeq)
        throw std::runtime_error("Capture path too long or mailbox busy");
    wcsncpy_s(channel->output, output.c_str(), _TRUNCATE);
    channel->frameCount = 90;
    channel->frameInterval = interval;
    const long seq = InterlockedIncrement(&channel->requestSeq);
    const auto captureEnd = (std::min)(bound.end, NowNs() + 10000000000ULL);
    while (channel->doneSeq != seq) {
        bound.Check();
        if (NowNs() >= captureEnd || channel->requestSeq != seq)
            throw std::runtime_error("Capture deadline or mailbox ownership changed");
        Sleep(20);
    }
    bound.Check();
    const CaptureCompletion observed{seq, channel->doneSeq, channel->status, channel->framesWritten,
        channel->width, channel->height, channel->renderer, channel->backbufferFormat};
    if (channel->requestSeq != seq || channel->doneSeq != seq || observed.framesWritten != 90 ||
        observed.nativeStatus != static_cast<std::int32_t>(kh2coop::CaptureStatus::Ok) ||
        observed.width == 0 || observed.height == 0)
        throw std::runtime_error("Capture native completion refused");
    return observed;
}
} // namespace owned_clip

CommandResult CmdOwnedClip(std::vector<std::string> args) {
    using namespace owned_clip;
    const auto required = [&](const char* key) { auto value = ConsumeOption(args, key); if (!value || value->empty()) throw std::runtime_error(std::string("Required ") + key); return *value; };
    const auto out = std::filesystem::path(required("--out"));
    const auto creation = ParseNumber<std::uint64_t>(required("--expected-creation"), "--expected-creation");
    const auto base = ParseNumber<std::uint64_t>(required("--expected-module"), "--expected-module");
    const auto deadline = ParseNumber<std::uint64_t>(required("--deadline-ns"), "--deadline-ns");
    const auto cancelName = required("--cancel-event");
    const auto toolHash = required("--tool-sha256");
    const auto gameHash = required("--game-sha256");
    if (required("--seconds") != "3" || required("--fps") != "30" || !args.empty()) throw std::runtime_error("Clip takes exactly3 seconds/30fps and exact options");
    if (!out.is_absolute() || out.extension() != L".mp4" ||
        (out.filename() != L"hb_arrival_peer0.mp4" && out.filename() != L"hb_arrival_peer1.mp4") ||
        std::filesystem::exists(out) || toolHash.size() != 64 || gameHash.size() != 64 || creation == 0 || base < 65536)
        throw std::runtime_error("Fresh explicit clip output/identity/pins required");
    const auto evidence = std::filesystem::canonical(out.parent_path());
    if (evidence != out.parent_path()) throw std::runtime_error("Clip evidence root must be canonical");
    const auto stem = out.stem().string();
    if (cancelName.rfind("Local\\kh2coop_clip_cancel_", 0) != 0) throw std::runtime_error("Explicit media cancellation event required");
    Handle cancel(OpenEventW(SYNCHRONIZE, FALSE, std::filesystem::path(cancelName).c_str()));
    if (!cancel) throw std::runtime_error("Media cancellation event unavailable");
    Bound bound{deadline, cancel.value}; bound.Check();
    const DWORD pid = ResolveTargetPid();
    Handle game(OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | SYNCHRONIZE, FALSE, pid));
    if (!game || ProcessCreationTime(game.value) != creation || ModuleBase(game.value) != base || WaitForSingleObject(game.value, 0) != WAIT_TIMEOUT)
        throw std::runtime_error("Original clip game identity/module refused");
    const auto gameImage = ImagePath(game.value);
    if (_wcsicmp(gameImage.filename().c_str(), kKh2ExeName) != 0) throw std::runtime_error("Clip target image refused");
    const auto record = OwnedRecord(pid, creation);
    if (HashFile(gameImage, bound) != gameHash) throw std::runtime_error("Clip game image pin mismatch");
    const auto tool = std::filesystem::canonical(RepoRoot() / "bin/ffmpeg.exe");
    if (tool.parent_path() != std::filesystem::canonical(RepoRoot() / "bin") || HashFile(tool, bound) != toolHash)
        throw std::runtime_error("Pinned package-local encoder/verifier required");
    std::ostringstream target;
    target << "{\"pid\":" << pid << ",\"creationTicks\":" << creation << ",\"moduleBase\":" << base
           << ",\"imagePath\":" << JsonString(gameImage.string()) << ",\"imageName\":\"KINGDOM HEARTS II FINAL MIX.exe\""
           << ",\"imageSha256\":" << JsonString(gameHash) << ",\"ownedRecordSha256\":" << JsonString(HashBytes(record))
           << ",\"handleHeldThroughFinalization\":true}";
    const auto rawDir = evidence / (stem + "_raw");
    const auto decodedDir = evidence / (stem + "_frames");
    if (!std::filesystem::create_directory(rawDir) || !std::filesystem::create_directory(decodedDir)) throw std::runtime_error("Fresh frame directories required");
    CaptureCompletion completion{}; double rate = 0, during = 0; std::uint32_t interval = 0;
    std::uint64_t started = 0, finished = 0;
    {
        kh2coop::CaptureLease lease;
        if (lease.Acquire(pid) != kh2coop::CaptureLeaseStatus::Acquired) throw std::runtime_error("Clip CaptureLease refused");
        CaptureChannelView channel(pid); if (!channel.get()) throw std::runtime_error("Owned capture channel unavailable");
        bound.Check(); rate = MeasurePresentFps(channel.get(), 500); bound.Check();
        if (!std::isfinite(rate) || rate < 1) throw std::runtime_error("Game not presenting");
        interval = static_cast<std::uint32_t>((std::max)(1.0, std::round(rate / 30)));
        started = NowNs(); const long presents = channel.get()->presentCount;
        completion = Capture(channel.get(), (rawDir / "f_%05u.bmp").wstring(), interval, bound);
        finished = NowNs(); bound.Check();
        during = (channel.get()->presentCount - presents) * 1e9 / static_cast<double>(finished - started);
    }
    Handle job(CreateJobObjectW(nullptr, nullptr)); JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!job || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) throw std::runtime_error("Owned media job unavailable");
    const double encodedFps = rate / interval; std::ostringstream fps; fps << std::setprecision(17) << encodedFps;
    const std::vector<std::string> encode{tool.string(), "-nostdin", "-n", "-loglevel", "error", "-threads", "1", "-framerate", fps.str(), "-start_number", "0", "-i", (rawDir / "f_%05d.bmp").string(), "-frames:v", "90", "-c:v", "libx264", "-threads", "1", "-pix_fmt", "yuv420p", "-crf", "20", out.string()};
    const auto encoder = Child(encode, toolHash, evidence, stem, "encoder", job.value, bound);
    const std::vector<std::string> decode{tool.string(), "-nostdin", "-n", "-xerror", "-loglevel", "info", "-threads", "1", "-err_detect", "explode", "-i", out.string(), "-vf", "format=rgb24,showinfo", "-fps_mode", "passthrough", "-c:v", "png", "-threads", "1", "-compression_level", "1", "-start_number", "0", (decodedDir / "decoded_%03d.png").string()};
    const auto verifier = Child(decode, toolHash, evidence, stem, "verifier", job.value, bound);
    bound.Check();
    if (ProcessCreationTime(game.value) != creation || ModuleBase(game.value) != base || WaitForSingleObject(game.value, 0) != WAIT_TIMEOUT || OwnedRecord(pid, creation) != record)
        throw std::runtime_error("Original target changed during media");
    std::ostringstream capture;
    capture << std::setprecision(17) << "{\"expectedRequestSeq\":" << completion.expectedRequestSeq << ",\"doneSeq\":" << completion.doneSeq
            << ",\"nativeStatus\":" << completion.nativeStatus << ",\"framesWritten\":" << completion.framesWritten
            << ",\"requestedInterval\":" << interval << ",\"width\":" << completion.width << ",\"height\":" << completion.height
            << ",\"renderer\":" << completion.renderer << ",\"backbufferFormat\":" << completion.backbufferFormat
            << ",\"gameFpsBefore\":" << rate << ",\"gameFpsDuringCapture\":" << during << ",\"encodedFps\":" << encodedFps
            << ",\"startedNs\":" << started << ",\"finishedNs\":" << finished << ",\"captureLeaseReleased\":true}";
    return {0, "{\"ok\":true,\"target\":" + target.str() + ",\"capture\":" + capture.str() + ",\"processes\":{\"encoder\":" + encoder.json + ",\"verifier\":" + verifier.json + "}}"};
}
