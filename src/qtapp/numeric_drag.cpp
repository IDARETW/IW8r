#include "numeric_drag.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPointer>
#include <QSpinBox>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace iw8r::qt::numeric_drag {

namespace {

bool numeric(const QAbstractSpinBox* s) { return qobject_cast<const QDoubleSpinBox*>(s) || qobject_cast<const QSpinBox*>(s); }

// The spin box of a field's text box (the mouse events go to the line edit inside it).
QAbstractSpinBox* spinOf(QObject* o) {
    auto* le = qobject_cast<QLineEdit*>(o);
    auto* s = le ? qobject_cast<QAbstractSpinBox*>(le->parentWidget()) : nullptr;
    return s && numeric(s) && s->isEnabled() && !s->isReadOnly() ? s : nullptr;
}

double valueOf(const QAbstractSpinBox* s) {
    if (auto* d = qobject_cast<const QDoubleSpinBox*>(s)) return d->value();
    if (auto* i = qobject_cast<const QSpinBox*>(s)) return i->value();
    return 0;
}

double stepOf(const QAbstractSpinBox* s) {
    if (auto* d = qobject_cast<const QDoubleSpinBox*>(s)) return d->singleStep() > 0 ? d->singleStep() : 1.0;
    if (auto* i = qobject_cast<const QSpinBox*>(s)) return std::max(1, i->singleStep());
    return 1;
}

void setValueOf(QAbstractSpinBox* s, double v) {
    if (auto* d = qobject_cast<QDoubleSpinBox*>(s)) d->setValue(v);
    else if (auto* i = qobject_cast<QSpinBox*>(s)) i->setValue(int(std::lround(v)));
}

class Filter : public QObject {
public:
    Filter() {
        timer_ = new QTimer(this);
        timer_->setInterval(30);
        connect(timer_, &QTimer::timeout, this, [this] { tick(); });
    }
    std::function<void(const QString&)> begin;
    std::function<void()> end;
    bool scrubbing() const { return scrubbing_; }

protected:
    bool eventFilter(QObject* o, QEvent* e) override {
        switch (e->type()) {
        case QEvent::Enter:
        case QEvent::FocusIn:
        case QEvent::FocusOut:
            // The sideways arrow over a field that can be dragged; the text cursor while typing in it.
            if (QAbstractSpinBox* s = spinOf(o)) {
                auto* le = static_cast<QLineEdit*>(o);
                const bool typing = e->type() == QEvent::FocusIn || (e->type() == QEvent::Enter && le->hasFocus());
                le->setCursor(typing ? Qt::IBeamCursor : Qt::SizeHorCursor);
                (void)s;
            }
            break;
        case QEvent::MouseButtonPress: {
            const auto* me = static_cast<QMouseEvent*>(e);
            QAbstractSpinBox* s = me->button() == Qt::LeftButton ? spinOf(o) : nullptr;
            if (!s || static_cast<QLineEdit*>(o)->hasFocus()) break;  // typing in it: the press places the caret as usual
            spin_ = s;
            edit_ = static_cast<QLineEdit*>(o);
            pressed_ = true;
            scrubbing_ = false;
            origin_ = me->globalPosition().toPoint();
            break;  // a click that does not move is the usual click
        }
        case QEvent::MouseMove: {
            if (!pressed_ || o != edit_ || !spin_) break;
            const auto* me = static_cast<QMouseEvent*>(e);
            if (!(me->buttons() & Qt::LeftButton)) {
                pressed_ = false;
                if (scrubbing_) finish(false);
                break;
            }
            const QPoint g = me->globalPosition().toPoint();
            if (!scrubbing_) {
                const int dx = g.x() - origin_.x(), dy = g.y() - origin_.y();
                if (std::abs(dx) < QApplication::startDragDistance() || std::abs(dx) < std::abs(dy)) break;
                scrubbing_ = true;
                original_ = valueOf(spin_);
                rebase(g, me->modifiers());
                edit_->deselect();
                QApplication::setOverrideCursor(Qt::SizeHorCursor);
                if (begin) begin(fieldName(spin_));
                timer_->start();
            }
            if (me->modifiers() != mods_) rebase(g, me->modifiers());  // Shift / Ctrl changed: no jump
            dx_ = g.x() - origin_.x();
            apply();
            return true;
        }
        case QEvent::MouseButtonRelease:
            // The field's release ends the drag; so does any release once the field is gone (a panel rebuilt it).
            if (!pressed_ || (o != edit_ && edit_)) break;
            pressed_ = false;
            if (!scrubbing_) break;
            finish(false);
            return true;
        case QEvent::KeyPress:
            if (scrubbing_ && o == edit_ && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
                pressed_ = false;
                finish(true);
                return true;
            }
            break;
        default:
            break;
        }
        return QObject::eventFilter(o, e);
    }

private:
    double pxPerStep() const { return mods_ & Qt::ShiftModifier ? 16.0 : 4.0; }
    double stepSize() const { return stepOf(spin_) * (mods_ & Qt::ControlModifier ? 10.0 : 1.0); }
    void rebase(const QPoint& at, Qt::KeyboardModifiers mods) {
        start_ = valueOf(spin_);
        origin_ = at;
        extra_ = 0;
        dx_ = 0;
        mods_ = mods;
    }
    void apply() {
        if (!spin_) return;
        // Whole steps from the start (grid-like values stay on their steps).
        const double steps = std::trunc(double(dx_) / pxPerStep()) + std::trunc(extra_);
        setValueOf(spin_, start_ + steps * stepSize());
    }
    void tick() {
        if (scrubbing_ && !spin_) return finish(false);  // the field went away mid-drag: the cursor is given back
        if (!scrubbing_) return timer_->stop();
        const int over = std::abs(dx_) - kZone;
        if (over <= 0) return;
        // Past the zone: a step every tick at its edge, up to 8 a tick further out.
        extra_ += (dx_ > 0 ? 1.0 : -1.0) * std::min(8.0, 1.0 + over / 40.0);
        apply();
    }
    void finish(bool cancel) {
        scrubbing_ = false;
        pressed_ = false;
        timer_->stop();
        QApplication::restoreOverrideCursor();
        if (spin_) {
            if (cancel) setValueOf(spin_, original_);
            emit spin_->editingFinished();  // fields that commit on it do so once
            spin_->clearFocus();            // the next drag works at once; a click still gives the caret
        }
        if (end) end();
    }

    QPointer<QAbstractSpinBox> spin_;
    QPointer<QLineEdit> edit_;
    bool pressed_ = false, scrubbing_ = false;
    QPoint origin_;
    double start_ = 0, original_ = 0, extra_ = 0;
    int dx_ = 0;
    Qt::KeyboardModifiers mods_;
    QTimer* timer_;
};

Filter*& filterRef() {
    static Filter* f = nullptr;
    return f;
}

}  // namespace

void install() {
    if (filterRef() || !qApp) return;
    filterRef() = new Filter;
    filterRef()->setParent(qApp);
    qApp->installEventFilter(filterRef());
}

void setHooks(std::function<void(const QString&)> begin, std::function<void()> end) {
    install();
    if (!filterRef()) return;
    filterRef()->begin = std::move(begin);
    filterRef()->end = std::move(end);
}

bool dragging() { return filterRef() && filterRef()->scrubbing(); }

QString fieldName(const QAbstractSpinBox* s) {
    const auto clean = [](QString t) {
        t.remove('&');
        while (t.endsWith(':')) t.chop(1);
        return t.trimmed();
    };
    if (QWidget* p = s->parentWidget()) {
        for (const QFormLayout* form : p->findChildren<QFormLayout*>())
            if (auto* l = qobject_cast<QLabel*>(form->labelForField(const_cast<QAbstractSpinBox*>(s)))) return clean(l->text());
        if (auto* form = qobject_cast<QFormLayout*>(p->layout()))
            if (auto* l = qobject_cast<QLabel*>(form->labelForField(const_cast<QAbstractSpinBox*>(s)))) return clean(l->text());
        QList<QGridLayout*> grids = p->findChildren<QGridLayout*>();
        if (auto* g = qobject_cast<QGridLayout*>(p->layout())) grids.prepend(g);
        for (QGridLayout* grid : grids) {
            const int i = grid->indexOf(const_cast<QAbstractSpinBox*>(s));
            if (i < 0) continue;
            int r = 0, c = 0, rs = 0, cs = 0;
            grid->getItemPosition(i, &r, &c, &rs, &cs);
            if (c > 0)
                if (QLayoutItem* it = grid->itemAtPosition(r, c - 1))
                    if (auto* l = qobject_cast<QLabel*>(it->widget())) return clean(l->text());
        }
    }
    if (!s->toolTip().isEmpty()) return clean(s->toolTip().section('\n', 0, 0).left(40));
    return "value";
}

}  // namespace iw8r::qt::numeric_drag
