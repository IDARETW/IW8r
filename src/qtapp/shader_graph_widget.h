#pragma once
#include <QWidget>
#include <QString>
#include <functional>
#include <set>
#include "shader_graph.h"

class QGraphicsScene;
class QGraphicsView;
class QUndoStack;
class QPlainTextEdit;

namespace iw8r::qt {
class ShaderGraphWidget : public QWidget {
    Q_OBJECT
  public:
    explicit ShaderGraphWidget(QWidget *parent = nullptr);
    ~ShaderGraphWidget() override;
    void setGraph(const ShaderGraph &graph, ShaderStage stage);
    const ShaderGraph &graph() const { return graph_; }
    void focusNode(const QString &id);
    void setDiagnostics(const std::vector<ShaderDiagnostic> &diagnostics);
    std::function<void(const ShaderGraph &)> onChanged;
    std::function<void(const std::string &)> onPreview;
    void addNode(const std::string &kind);
    void connectPorts(const std::string &from, const std::string &to, const std::string &input);
    void undo();
    void redo();

  private:
    void rebuild();
    void commit(const ShaderGraph &before, const QString &label);
    void restore(const ShaderGraph &graph);
    void chooseNode();
    void editNode(const std::string &id);
    void deleteSelected();
    void copy();
    void paste();
    void makeSubgraph();
    void selectionChanged();
    ShaderGraph graph_;
    ShaderStage stage_ = ShaderStage::Pixel;
    QGraphicsScene *scene_ = nullptr;
    QGraphicsView *view_ = nullptr;
    QUndoStack *undo_ = nullptr;
    QPlainTextEdit *properties_ = nullptr;
    std::string selected_, pendingOutput_;
    bool rebuilding_ = false;
    std::set<std::string> errors_;
};
} // namespace iw8r::qt
