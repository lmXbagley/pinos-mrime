#pragma once
// mp/undo.hpp — единая история для действий мыши И кода (7.3), глубина 1024.
#include "mp/model.hpp"

#include <string>
#include <vector>

namespace mp {

class UndoStack {
public:
  explicit UndoStack(int depth = 1024) : depth_(depth) {}

  // снять снапшот ДО мутации; label — описание ("мышь: удалить связь", "код: Сборка")
  void push(const Project& doc, const std::string& label);

  // история сборок кода (4.6): каждая успешная сборка запоминает снимок кода
  void pushCodeBuild(const std::string& codeText);

  bool canUndo() const { return pos_ > 0; }
  bool canRedo() const { return pos_ < (int)stack_.size(); }
  Project undo(const Project& cur, std::string* label = nullptr);
  Project redo(const Project& cur, std::string* label = nullptr);

  int size() const { return (int)stack_.size(); }
  const std::vector<std::string>& buildHistory() const { return buildHistory_; }

private:
  struct Entry {
    Project doc;
    std::string label;
  };
  std::vector<Entry> stack_;
  std::vector<std::string> buildHistory_;
  int pos_ = 0;
  int depth_ = 1024;
};

} // namespace mp
