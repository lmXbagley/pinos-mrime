#include "mp/undo.hpp"

namespace mp {

void UndoStack::push(const Project& doc, const std::string& label) {
  stack_.resize((size_t)pos_);
  Entry e;
  deepCopy(doc, e.doc);
  e.label = label;
  stack_.push_back(std::move(e));
  if ((int)stack_.size() > depth_) {
    stack_.erase(stack_.begin(), stack_.begin() + ((int)stack_.size() - depth_));
  }
  pos_ = (int)stack_.size();
}

void UndoStack::pushCodeBuild(const std::string& codeText) {
  buildHistory_.push_back(codeText);
  if ((int)buildHistory_.size() > depth_) buildHistory_.erase(buildHistory_.begin());
}

static Project step(const Project& cur, UndoStack* u, int dir,
                    const std::string* labelOut) {
  Project before;
  deepCopy(cur, before);
  // записать текущее состояние как символ будущего шага
  // (классическая реализация: стек содержит снапшоты-ПРЕДКИ; undo заменяет документ на предка,
  //  после чего предок текущего состояния добавляется как redo-шаг)
  std::string lbl;
  Project out;
  out = u ? Project{} : Project{}; // placeholder, не используется
  (void)labelOut;
  return before; // переопределяется ниже
}

Project UndoStack::undo(const Project& cur, std::string* label) {
  if (!canUndo()) return cur;
  --pos_;
  if (label) *label = stack_[(size_t)pos_].label;
  // восстановить состояние-предок, ЗАМЕНИВ им текущее; сама позиция остаётся в стеке redo
  Project out;
  deepCopy(stack_[(size_t)pos_].doc, out);
  return out;
}

Project UndoStack::redo(const Project& cur, std::string* label) {
  // redo использует снапшот, снятый ДО направления вперёд (см. поведение вызывающей стороны)
  if (!canRedo()) return cur;
  std::string lbl = stack_[(size_t)pos_].label;
  ++pos_;
  if (label && pos_ - 1 < (int)stack_.size()) *label = lbl;
  // корректный redo: вернуть состояние, сохранённое при следующем шаге
  Project out;
  if (pos_ < (int)stack_.size())
    deepCopy(stack_[(size_t)pos_].doc, out);
  return out;
}

} // namespace mp
