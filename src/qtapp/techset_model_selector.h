#pragma once
#include <QDialog>
namespace iw8r::qt {
class AssetBrowser;
class TechsetModelSelector : public QDialog {
    Q_OBJECT
  public:
    explicit TechsetModelSelector(AssetBrowser *browser, QWidget *parent = nullptr);
    QString selectedModel() const;
  signals:
    void modelSelected(const QString &name);

  private:
    AssetBrowser *browser_;
};
} // namespace iw8r::qt
