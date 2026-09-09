param()
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$builder = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$project = Join-Path $PSScriptRoot 'ShellTests.vcxproj'
$output = Join-Path $repo 'build\shell-tests'
$reports = Join-Path $repo 'build\parity-mutations'
New-Item -ItemType Directory -Force -Path $reports | Out-Null
# Deliberate regressions, restored even when a mutant fails to build or run.
#
# These were ui/ only while every parity item was panel work. The MIDI output
# cases below are in MIDI++/ because that is where the engine half of it lives
# and the engine seat owns both those files and this runner. The rule the
# original comment was reaching for still holds: never mutate a file the other
# seat is working in.
$cases = @(
    @{Name='out-range-autoplay-release'; File='ui\ShellEngine.cpp'; Group='out-range'; Start='case Action::EightyEightKeys:'; End='case Action::AutoVolumeScan:'; Find='stopPlayback();'; Replace='if (layoutChange) stopPlayback();'; Failure='OutRange switch left the folded autoplay key held'},
    @{Name='out-range-live-release'; File='ui\ShellEngine.cpp'; Group='out-range'; Start='case Action::EightyEightKeys:'; End='case Action::AutoVolumeScan:'; Find='if (live) { player->release_every_mapped_key(); live.reset(); }'; Replace='if (live) { live.reset(); }'; Failure='OutRange switch left the folded live key held'},
    @{Name='same-name-device-merge'; File='ui\DeviceModel.hpp'; Group='grouping'; Find='return item.second > 1;'; Replace='return false;'; Failure='same-name devices must fall back to individual ids'},
    @{Name='wide-stderr-invisible'; File='ui\ShellLog.hpp'; Group='log'; Find='wideErrOld_(std::wcerr.rdbuf(&wideErr_))'; Replace='wideErrOld_(std::wcerr.rdbuf())'; Failure='wide KS errors preserve Unicode'},
    @{Name='countdown-types-early'; File='ui\ShellEngine.cpp'; Group='countdown'; Find='playbackDue = std::chrono::steady_clock::now() + std::chrono::seconds(state.playbackDelay);'; Replace='playbackDue = std::chrono::steady_clock::now() + std::chrono::seconds(state.playbackDelay); startPlayback();'; Failure='countdown typed a note before expiry'},
    @{Name='warning-gate-bypassed'; File='ui\ShellEngine.cpp'; Group='countdown'; Start='const auto startPlayback ='; End='const auto applyMappings ='; Find='if (!state.typingAcknowledged)'; Replace='if (false && !state.typingAcknowledged)'; Failure='unacknowledged autoplay was not rejected'},
    @{Name='legit-disabled-on-load'; File='ui\ShellEngine.cpp'; Group='library'; Start='case Action::Load: {'; End='case Action::TogglePlayPause:'; Find='player->legit_mode_active = state.legitMode;'; Replace='player->legit_mode_active = false;'; Failure='Load reset the real Legit Mode flag'},
    @{Name='stop-leaves-midiconnect-on'; File='ui\ShellEngine.cpp'; Group='connect'; Start='case Action::Stop:'; End='case Action::Mute:'; Find='stopConnect();'; Replace='/* omitted close */'; Failure='Stop did not close MidiConnect'},
    @{Name='stop-allows-queued-shuffle'; File='ui\ShellEngine.cpp'; Group='library'; Find='if (command.amount == 1 && (!shuffleAdvancePending || !state.shuffle)) break;'; Replace='/* stale advance accepted */'; Failure='Stop allowed shuffle to start another song'},
    # Snaps the input back onto the 32-point uniform grid, which is the drift
    # the resampling had: a grid stepping by 4.097 across a table stepping by 4.
    @{Name='curve-resampled-on-uniform-grid'; File='ui\VelocityModel.hpp'; Group='curve'; Find='const float input = std::clamp(x, 0.f, 1.f) * 127;'; Replace='const float input = std::round(std::round(std::clamp(x, 0.f, 1.f) * 31) * 127.f / 31);'; Failure='the drawn curve misses a point the threshold table names'},
    # The ordering MIDI-OUTPUT.md says will be got wrong if it is not written
    # down: a switch that stores the new target without stopping the old one
    # leaves a note sounding on the synth with nothing left to address it.
    @{Name='switch-strands-the-held-note'; File='MIDI++\PlaybackCore.cpp'; Group='midi-out'; Start='void VirtualPianoPlayer::set_output_target'; End='int VirtualPianoPlayer::toggle_transpose_adjustment'; Find='silence_midi_output();'; Replace='/* outgoing target left sounding */'; Failure='switching away from MIDI sent nothing to stop the held note'},
    # Velocity travels in the note-on byte on this target, so losing it there
    # loses it entirely: there is no ALT tap on the wire to fall back to.
    @{Name='midi-note-drops-velocity'; File='MIDI++\PlaybackCore.cpp'; Group='midi-out'; Find='static_cast<uint8_t>(event.velocity & 0x7F) };'; Replace='127 };'; Failure='velocity reaches the note-on byte, at the pitch that was played'}
)
function Build-Tests([string]$name) {
    & $builder $project /p:Configuration=Release /p:Platform=x64 /m /v:quiet /nologo *> (Join-Path $reports "$name-build.log")
    if ($LASTEXITCODE -ne 0) { throw "Build failed: $name. See build/parity-mutations." }
}
function Run-Group([string]$group, [string]$name) {
    Push-Location -LiteralPath $output
    try {
        $ErrorActionPreference = 'Continue'
        & '.\ShellTests.exe' $group *> (Join-Path $reports "$name.log")
        return $LASTEXITCODE
    } finally { Pop-Location }
}
Build-Tests 'baseline'
Copy-Item -LiteralPath (Join-Path $repo 'x64\Release\config.json') -Destination (Join-Path $output 'config.json')
foreach ($group in ($cases.Group | Select-Object -Unique)) {
    if ((Run-Group $group "baseline-$group") -ne 0) { throw "Baseline failed: $group" }
}
try {
    foreach ($case in $cases) {
        $path = Join-Path $repo $case.File
        $original = [IO.File]::ReadAllBytes($path)
        try {
            $source = [Text.Encoding]::UTF8.GetString($original)
            $begin = 0; $end = $source.Length
            if ($case.Start) {
                $begin = $source.IndexOf($case.Start, [StringComparison]::Ordinal)
                if ($begin -lt 0) { throw "Mutation start missing: $($case.Name)" }
                $end = $source.IndexOf($case.End, $begin, [StringComparison]::Ordinal)
                if ($end -lt 0) { throw "Mutation end missing: $($case.Name)" }
            }
            $section = $source.Substring($begin, $end - $begin)
            if (($section.Split(@($case.Find), [StringSplitOptions]::None).Length - 1) -ne 1) {
                throw "Mutation anchor is missing or ambiguous: $($case.Name)"
            }
            $mutated = $source.Substring(0, $begin) + $section.Replace($case.Find, $case.Replace) + $source.Substring($end)
            [IO.File]::WriteAllText($path, $mutated, [Text.UTF8Encoding]::new($false))
            Build-Tests $case.Name
            $result = Run-Group $case.Group $case.Name
            $log = Get-Content -Raw -LiteralPath (Join-Path $reports "$($case.Name).log")
            if ($result -eq 0 -or !$log.Contains($case.Failure)) {
                throw "Mutation survived or failed for the wrong reason: $($case.Name)"
            }
            Write-Output "KILLED $($case.Name): $($case.Failure)"
        } finally { [IO.File]::WriteAllBytes($path, $original) }
    }
} finally { Build-Tests 'restored' }
foreach ($group in ($cases.Group | Select-Object -Unique)) {
    if ((Run-Group $group "restored-$group") -ne 0) { throw "Restored baseline failed: $group" }
}
Write-Output 'PASS all parity mutations; sources restored and baseline groups passed'
