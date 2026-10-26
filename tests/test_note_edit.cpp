#include <cmath>

#include "TestUtil.h"
#include "core/NoteEdit.h"
#include "core/NoteHistory.h"

using namespace ttr;

namespace {

constexpr double kEps = 1e-9;

TimeGrid grid120(int snapMode = 3, double startPpq = 0.0) {
    TimeGrid g;
    g.bpm = 120.0;  // 1 beat = 0.5 s
    g.startPpq = startPpq;
    g.snapMode = snapMode;
    return g;
}

// Energy matrix with one bright row (pitch 69) between 1 s and 3 s, a weaker neighbour at 70.
struct BrightRow {
    static constexpr int kFrames = 400;
    static constexpr double kHop = 0.01;
    std::vector<float> energies = std::vector<float>(size_t(kFrames) * kNumNotes, 0.0f);
    BrightRow() {
        for (int f = 100; f < 300; ++f) {
            energies[size_t(f) * kNumNotes + size_t(69 - kMinNote)] = 1.0f;
            energies[size_t(f) * kNumNotes + size_t(70 - kMinNote)] = 0.3f;
        }
    }
};

std::vector<NoteEvent> threeNotes() {
    return {makeNote(60, 0.0, 0.5, grid120(), true), makeNote(64, 0.5, 1.0, grid120(), true), makeNote(67, 1.0, 1.5, grid120(), true)};
}

}  // namespace

TEST(snap_grid_steps_and_modes) {
    CHECK(gridStepSec(grid120(0)) == 0.0);
    CHECK(std::fabs(gridStepSec(grid120(1)) - 0.5) < kEps);
    CHECK(std::fabs(gridStepSec(grid120(3)) - 0.125) < kEps);
    CHECK(std::fabs(gridStepSec(grid120(4)) - 0.0625) < kEps);
    CHECK(std::string(snapName(3)) == "1/16" && std::string(snapName(0)) == "Off");
    CHECK(std::fabs(snapTime(1.05, grid120(3)) - 1.0) < kEps);
    CHECK(std::fabs(snapTime(1.07, grid120(3)) - 1.125) < kEps);  // nearest line wins
    CHECK(std::fabs(snapTime(1.07, grid120(0)) - 1.07) < kEps);  // off: unchanged
    CHECK(std::fabs(snapTime(1.3, grid120(1)) - 1.5) < kEps);
    // the grid follows the project beats: with startPpq 0.5 a quarter-note line sits half a beat (0.25 s) earlier
    CHECK(std::fabs(snapTime(0.9, grid120(1, 0.5)) - 0.75) < kEps);
}

TEST(draw_note_with_snap_orders_and_enforces_min_length) {
    const NoteEvent n = makeNote(69, 1.01, 1.49, grid120(), true);
    CHECK(n.pitch == 69 && std::fabs(n.startSec - 1.0) < kEps && std::fabs(n.endSec - 1.5) < kEps && !n.selected);
    const NoteEvent backwards = makeNote(69, 1.49, 1.01, grid120(), true);  // dragged to the left
    CHECK(std::fabs(backwards.startSec - 1.0) < kEps && std::fabs(backwards.endSec - 1.5) < kEps);
    const NoteEvent tiny = makeNote(60, 2.01, 2.02, grid120(), true);  // shorter than one step: one step
    CHECK(std::fabs(tiny.startSec - 2.0) < kEps && std::fabs(tiny.endSec - 2.125) < kEps);
    const NoteEvent free = makeNote(60, 0.333, 0.7, grid120(0), true);  // snap off
    CHECK(std::fabs(free.startSec - 0.333) < kEps && std::fabs(free.endSec - 0.7) < kEps);
    const NoteEvent alt = makeNote(60, 0.333, 0.7, grid120(3), false);  // Alt held: no snap although the grid is on
    CHECK(std::fabs(alt.startSec - 0.333) < kEps);
    CHECK(makeNote(60, -1.0, 0.3, grid120(0), true).startSec == 0.0);
    CHECK(makeNote(200, 0, 1, grid120(), true).pitch == kMaxNote && makeNote(1, 0, 1, grid120(), true).pitch == kMinNote);
}

TEST(magnet_pulls_to_the_strongest_row_within_one_semitone) {
    const BrightRow data;
    const float* e = data.energies.data();
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 1.2, 2.0, 70) == 69);  // 70 has energy, 69 is stronger
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 1.2, 2.0, 68) == 69);
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 1.2, 2.0, 69) == 69);
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 1.2, 2.0, 71) == 70);  // only +-1: reaches 70, not 69
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 1.2, 2.0, 66) == 66);  // too far away
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 3.5, 3.9, 70) == 70);  // silence keeps the cursor row
    CHECK(magnetPitch(e, BrightRow::kFrames, BrightRow::kHop, 2.0, 1.2, 70) == 69);  // reversed span
    CHECK(magnetPitch(nullptr, 0, 0.0, 0, 1, 70) == 70);
}

TEST(add_merges_same_pitch_overlaps_and_sorts) {
    auto notes = withNoteAdded({}, makeNote(60, 1.0, 1.5, grid120(), true));
    notes = withNoteAdded(notes, makeNote(60, 1.25, 2.0, grid120(), true));  // overlaps: one longer note
    CHECK(notes.size() == 1 && std::fabs(notes[0].startSec - 1.0) < kEps && std::fabs(notes[0].endSec - 2.0) < kEps);
    notes = withNoteAdded(notes, makeNote(64, 0.0, 0.5, grid120(), true));
    CHECK(notes.size() == 2 && notes[0].pitch == 64);  // sorted by start time
    notes = withNoteAdded(notes, makeNote(60, 2.0, 2.5, grid120(), true));  // touching, not overlapping: stays separate
    CHECK(notes.size() == 3);
}

TEST(move_changes_time_and_pitch_only_for_the_chosen_notes) {
    const auto notes = threeNotes();
    const auto moved = withMoved(notes, {1}, 0.25, 2);
    CHECK(moved.size() == 3);
    bool found = false;
    for (const NoteEvent& n : moved) found |= n.pitch == 66 && std::fabs(n.startSec - 0.75) < kEps && std::fabs(n.endSec - 1.25) < kEps;
    CHECK(found);
    CHECK(moved[0].pitch == 60 && moved.back().pitch == 67);  // the others stay
    const auto clampedTime = withMoved(notes, {0, 1}, -5.0, 0);  // never before time 0, the group keeps its shape
    CHECK(clampedTime[0].startSec == 0.0 && std::fabs(clampedTime[1].startSec - 0.5) < kEps);
    const auto clampedPitch = withMoved(notes, {2}, 0.0, 100);
    CHECK(clampedPitch.back().pitch == kMaxNote || clampedPitch[0].pitch == kMaxNote);
    CHECK(withMoved(notes, {9, -1}, 1.0, 1).size() == 3);  // bad indices ignored
}

TEST(resize_moves_one_edge_and_keeps_a_minimum_length) {
    const auto notes = threeNotes();
    const auto longer = withResized(notes, 0, false, 1.5, 0.125);
    CHECK(std::fabs(longer[0].endSec - 1.5) < kEps && longer[0].startSec == 0.0);
    const auto earlier = withResized(notes, 1, true, 0.25, 0.125);
    CHECK(std::fabs(earlier[0].startSec - 0.25) < kEps || std::fabs(earlier[1].startSec - 0.25) < kEps);
    const auto crushed = withResized(notes, 0, false, 0.0, 0.125);  // cannot get shorter than the minimum
    CHECK(std::fabs(crushed[0].endSec - 0.125) < kEps);
    const auto overshoot = withResized(notes, 0, true, 9.0, 0.125);  // start cannot pass the end
    CHECK(std::fabs(overshoot[0].startSec - 0.375) < kEps);
    CHECK(withResized(notes, 5, true, 0.0, 0.1).size() == 3);
}

TEST(delete_selected_and_by_index) {
    auto notes = withSelection(threeNotes(), {0, 2}, SelectMode::Replace);
    CHECK(selectedCount(notes) == 2);
    const auto rest = withSelectedDeleted(notes);
    CHECK(rest.size() == 1 && rest[0].pitch == 64);
    CHECK(withDeleted(notes, {1}).size() == 2 && withDeleted(notes, {7}).size() == 3);
    CHECK(withSelectedDeleted(withAllSelected(notes, false)).size() == 3);
}

TEST(duplicate_places_copies_behind_the_selection) {
    auto notes = withSelection(threeNotes(), {0, 1}, SelectMode::Replace);  // spans 0.0 .. 1.0
    const auto dup = withDuplicated(notes);
    CHECK(dup.size() == 5);
    CHECK(selectedCount(dup) == 2);  // the copies are selected, the originals are not
    for (const NoteEvent& n : dup)
        if (n.selected) CHECK(n.startSec >= 1.0 - kEps && (n.pitch == 60 || n.pitch == 64));
    CHECK(withDuplicated(withAllSelected(notes, false)).size() == 3);  // nothing selected: nothing happens
    CHECK(withDuplicated({}).empty());
}

TEST(selection_modes_and_lasso_region) {
    const auto notes = threeNotes();
    auto sel = withSelection(notes, {0}, SelectMode::Replace);
    sel = withSelection(sel, {1}, SelectMode::Add);
    CHECK(selectedCount(sel) == 2);
    sel = withSelection(sel, {0}, SelectMode::Toggle);
    CHECK(selectedCount(sel) == 1 && !sel[0].selected && sel[1].selected);
    CHECK(selectedCount(withSelection(sel, {2}, SelectMode::Replace)) == 1);
    CHECK(selectedCount(withAllSelected(notes, true)) == 3 && selectedIndices(withAllSelected(notes, true)).size() == 3);
    CHECK(withSelection(notes, {99, -1}, SelectMode::Replace).size() == 3);
    const auto hit = indicesInRegion(notes, 0.4, 1.1, 63, 65);
    CHECK(hit.size() == 1 && hit[0] == 1);
    CHECK(indicesInRegion(notes, 1.1, 0.4, 70, 59).size() == 3);  // reversed corners
    CHECK(indicesInRegion(notes, 3.0, 4.0, 0, 127).empty());
}

TEST(history_undo_redo_depth_and_redo_branch) {
    NoteHistory h;
    std::vector<NoteEvent> cur;
    CHECK(!h.undo(&cur) && !h.redo(&cur) && !h.canUndo());
    for (int i = 0; i < 60; ++i) {  // more than the 50 steps the spec asks for
        h.record(cur);
        cur = withNoteAdded(cur, makeNote(40 + i % 40, i * 1.0, i * 1.0 + 0.5, grid120(), true));
    }
    CHECK(cur.size() == 60);
    int undone = 0;
    while (h.undo(&cur)) ++undone;
    CHECK(undone == 60 && cur.empty());
    CHECK(h.canRedo());
    CHECK(h.redo(&cur) && cur.size() == 1);
    h.record(cur);  // a new edit drops the redo branch
    cur = withNoteAdded(cur, makeNote(50, 70.0, 71.0, grid120(), true));
    CHECK(!h.canRedo());
    for (size_t i = 0; i < NoteHistory::kMaxSteps + 20; ++i) h.record(cur);
    int steps = 0;
    while (h.undo(&cur)) ++steps;
    CHECK(steps == int(NoteHistory::kMaxSteps));  // bounded
    h.clear();
    CHECK(!h.canUndo() && !h.canRedo());
}
