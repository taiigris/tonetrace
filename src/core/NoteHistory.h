// Undo / redo of the document: whole-list snapshots, bounded. A step is either a note list or, for Reset, the whole document.
#pragma once
#include <vector>

#include "core/Types.h"

namespace ttr {

class NoteHistory {
public:
    static constexpr size_t kMaxSteps = 100;
    enum class Applied { None, Notes, Document };

    // A new edit is about to replace `before`: remember it and drop the redo branch.
    void record(const std::vector<NoteEvent>& before) { push(Entry{before, nullptr, false}); }
    // The whole document (recording included, null = empty document) is about to be replaced (Reset).
    void recordDocument(const SnapshotPtr& before) { push(Entry{{}, before, true}); }

    // Swap the current state with the previous / next one. A notes step replaces `*notes`, a document step `*document`.
    Applied undo(std::vector<NoteEvent>* notes, SnapshotPtr* document) { return step(undo_, redo_, notes, document); }
    Applied redo(std::vector<NoteEvent>* notes, SnapshotPtr* document) { return step(redo_, undo_, notes, document); }
    // Notes-only convenience (no document steps are expected).
    bool undo(std::vector<NoteEvent>* notes) { SnapshotPtr d; return undo(notes, &d) != Applied::None; }
    bool redo(std::vector<NoteEvent>* notes) { SnapshotPtr d; return redo(notes, &d) != Applied::None; }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void clear() { undo_.clear(); redo_.clear(); }

private:
    struct Entry {
        std::vector<NoteEvent> notes;
        SnapshotPtr document;
        bool isDocument = false;
    };
    using Stack = std::vector<Entry>;
    void push(Entry e) {
        undo_.push_back(std::move(e));
        if (undo_.size() > kMaxSteps) undo_.erase(undo_.begin());
        redo_.clear();
    }
    static Applied step(Stack& from, Stack& to, std::vector<NoteEvent>* notes, SnapshotPtr* document) {
        if (from.empty()) return Applied::None;
        Entry target = std::move(from.back());
        from.pop_back();
        if (target.isDocument) {
            to.push_back(Entry{{}, *document, true});
            *document = std::move(target.document);
            return Applied::Document;
        }
        to.push_back(Entry{*notes, nullptr, false});
        *notes = std::move(target.notes);
        return Applied::Notes;
    }
    Stack undo_, redo_;
};

}  // namespace ttr
