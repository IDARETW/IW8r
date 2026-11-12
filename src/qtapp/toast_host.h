// IW8 Radiant Suite (Qt shell) - toast notifications: small non-modal boxes in the bottom-right corner of the main window
// for Warning / Error log entries and the compile result. Child widget of the window: it never takes focus or
// activation and ignores the mouse. At most three are shown; each disappears after about four seconds.
// View > Toast Notifications (ToggleToasts) turns them off; the choice is kept in Settings (view/toasts).
#pragma once

#include <QWidget>

#include "editor_log.h"

class QVBoxLayout;

namespace iw8r::qt {

class ToastHost : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMaxVisible = 3;
    static constexpr int kLifetimeMs = 4000;

    explicit ToastHost(QWidget* window);

    void showToast(LogLevel level, const QString& text, const QString& source = {});
    void setToastsEnabled(bool on);  // off hides and clears the toasts; the flag is saved by the caller (ToggleToasts)
    bool toastsEnabled() const { return enabled_; }
    int visibleCount() const;        // toasts on screen right now

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void dismiss(QWidget* toast);
    void relayout();

    QVBoxLayout* lay_ = nullptr;
    bool enabled_ = true;
};

}  // namespace iw8r::qt
