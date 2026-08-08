#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QQuickPaintedItem>
#include <QTimer>

class ThinkingOrbItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(bool running READ running WRITE setRunning NOTIFY runningChanged)
    Q_PROPERTY(bool reducedMotion READ reducedMotion WRITE setReducedMotion NOTIFY reducedMotionChanged)
    Q_PROPERTY(qreal speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(QColor tint READ tint WRITE setTint NOTIFY tintChanged)

public:
    explicit ThinkingOrbItem(QQuickItem *parent = nullptr);

    bool running() const { return m_running; }
    void setRunning(bool running);
    bool reducedMotion() const { return m_reducedMotion; }
    void setReducedMotion(bool reducedMotion);
    qreal speed() const { return m_speed; }
    void setSpeed(qreal speed);
    QColor tint() const { return m_tint; }
    void setTint(const QColor &tint);

    void paint(QPainter *painter) override;

signals:
    void runningChanged();
    void reducedMotionChanged();
    void speedChanged();
    void tintChanged();

private:
    void updateClock();

    QTimer m_frameTimer;
    QElapsedTimer m_elapsed;
    bool m_running = true;
    bool m_reducedMotion = false;
    qreal m_speed = 1.0;
    QColor m_tint = QColor(QStringLiteral("#e2e2e5"));
};
