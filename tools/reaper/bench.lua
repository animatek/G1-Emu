-- G1-Emu in REAPER, end to end (bench.ps1 runs it). G1_PHASE=new: a track with G1-Emu and a MIDI
-- item (note 60 on channel 1, twice a second), boot, play, save the project as bench.rpp in
-- G1_BENCH_DIR. G1_PHASE=reopen: that project is already open; just play. Then REAPER quits. The
-- verdict is in G1-Emu's log (the health line: the output level 1.5 s after MIDI in).
local here = os.getenv("G1_BENCH_DIR") or ({reaper.get_action_context()})[2]:match("^(.*)[/\\]")
local phase = os.getenv("G1_PHASE") or "new"
local boot = tonumber(os.getenv("G1_BOOT_SECONDS") or "20")
local secs = tonumber(os.getenv("G1_PLAY_SECONDS") or "12")
local out = io.open(here .. "/bench-" .. phase .. ".txt", "w")

if phase == "new" then
  reaper.InsertTrackAtIndex(0, true)
  local tr = reaper.GetTrack(0, 0)
  local fx = reaper.TrackFX_AddByName(tr, "VST3i:G1-Emu (Animatek)", false, -1)
  out:write("fx index " .. tostring(fx) .. "\n")
  local item = reaper.CreateNewMIDIItemInProj(tr, 0, secs, false)
  local take = reaper.GetActiveTake(item)
  for i = 0, secs * 2 - 1 do
    local s = reaper.MIDI_GetPPQPosFromProjTime(take, i * 0.5)
    local e = reaper.MIDI_GetPPQPosFromProjTime(take, i * 0.5 + 0.4)
    reaper.MIDI_InsertNote(take, false, false, s, e, 0, 60, 100, true)
  end
  reaper.MIDI_Sort(take)
else
  local tr = reaper.GetTrack(0, 0)
  out:write("tracks " .. reaper.CountTracks(0) .. ", fx " .. (tr and reaper.TrackFX_GetCount(tr) or -1) .. "\n")
end

local t0, state, tPlay, tStop = reaper.time_precise(), "boot", 0, 0
local function loop()
  local now = reaper.time_precise()
  if state == "boot" and now - t0 > boot then
    reaper.SetEditCurPos(0, false, false)
    reaper.OnPlayButton()
    state, tPlay = "play", now
  elseif state == "play" and now - tPlay > secs then
    reaper.OnStopButton()
    state, tStop = "stop", now
  elseif state == "stop" and now - tStop > 3 then
    out:write("played " .. secs .. " s\n")
    out:close()
    reaper.Main_SaveProjectEx(0, here .. "/bench.rpp", 0)
    -- An untitled project stays unsaved after that, and quitting would ask: switch to the saved one.
    if phase == "new" then reaper.Main_openProject("noprompt:" .. here .. "/bench.rpp") end
    reaper.Main_OnCommand(40004, 0)	-- File: Quit REAPER
    return
  end
  reaper.defer(loop)
end
loop()
