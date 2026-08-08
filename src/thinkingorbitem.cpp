#include "thinkingorbitem.h"

#include <QPainter>
#include <QVector3D>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {

constexpr qreal kPi = 3.14159265358979323846;

struct OrbDot {
    QPointF position;
    qreal depth = 0.0;
    qreal radius = 0.5;
    qreal brightness = 0.5;
    qreal opacity = 1.0;
};

qreal hash(qreal a, qreal b)
{
    const qreal value = std::sin(a * 12.9898 + b * 78.233) * 43758.5453;
    return value - std::floor(value);
}

QVector3D project(const QVector3D &point, qreal yaw, qreal tilt)
{
    const qreal sinYaw = std::sin(yaw);
    const qreal cosYaw = std::cos(yaw);
    const qreal sinTilt = std::sin(tilt);
    const qreal cosTilt = std::cos(tilt);
    const qreal x = point.x() * cosYaw + point.z() * sinYaw;
    const qreal z = -point.x() * sinYaw + point.z() * cosYaw;
    const qreal y = point.y() * cosTilt - z * sinTilt;
    const qreal projectedZ = point.y() * sinTilt + z * cosTilt;
    return QVector3D(x, y, projectedZ);
}

QColor shaded(const QColor &tint, qreal brightness, qreal opacity)
{
    QColor color = tint;
    color.setRedF(std::clamp(color.redF() * brightness, 0.0, 1.0));
    color.setGreenF(std::clamp(color.greenF() * brightness, 0.0, 1.0));
    color.setBlueF(std::clamp(color.blueF() * brightness, 0.0, 1.0));
    color.setAlphaF(std::clamp(opacity, 0.0, 1.0));
    return color;
}

} // namespace

ThinkingOrbItem::ThinkingOrbItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    setFillColor(Qt::transparent);
    setOpaquePainting(false);
    m_elapsed.start();
    m_frameTimer.setTimerType(Qt::PreciseTimer);
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, [this]() { update(); });
    connect(this, &QQuickItem::visibleChanged, this, &ThinkingOrbItem::updateClock);
    updateClock();
}

void ThinkingOrbItem::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    updateClock();
    update();
    emit runningChanged();
}

void ThinkingOrbItem::setReducedMotion(bool reducedMotion)
{
    if (m_reducedMotion == reducedMotion)
        return;
    m_reducedMotion = reducedMotion;
    updateClock();
    update();
    emit reducedMotionChanged();
}

void ThinkingOrbItem::setSpeed(qreal speed)
{
    speed = std::clamp(speed, 0.1, 3.0);
    if (qFuzzyCompare(m_speed, speed))
        return;
    m_speed = speed;
    update();
    emit speedChanged();
}

void ThinkingOrbItem::setTint(const QColor &tint)
{
    if (!tint.isValid() || m_tint == tint)
        return;
    m_tint = tint;
    update();
    emit tintChanged();
}

void ThinkingOrbItem::updateClock()
{
    if (m_running && !m_reducedMotion && isVisible())
        m_frameTimer.start();
    else
        m_frameTimer.stop();
}

void ThinkingOrbItem::paint(QPainter *painter)
{
    const qreal size = std::min(width(), height());
    if (size <= 2.0)
        return;

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    const qreal time = m_reducedMotion
        ? 0.72
        : (m_elapsed.elapsed() / 1000.0) * m_speed * 1.885;
    const QPointF center(width() / 2.0, height() / 2.0);
    const qreal sphereRadius = size * 0.41;
    const qreal yaw = time * 0.12;
    const qreal tilt = 0.3;
    const int orbitCount = size < 28.0 ? 5 : 9;
    const int ghostCount = size < 28.0 ? 16 : 28;
    const int particleCount = 3;
    const qreal scale = std::pow(size / 64.0, 0.6);

    QVector<OrbDot> dots;
    dots.reserve(orbitCount * (ghostCount + particleCount));
    for (int orbit = 0; orbit < orbitCount; ++orbit) {
        const qreal h1 = hash(orbit, 1.7);
        const qreal h2 = hash(orbit, 5.2);
        const qreal h3 = hash(orbit, 8.9);
        const qreal radius = sphereRadius * (0.45 + 0.52 * h1);
        const qreal theta = h1 * 2.0 * kPi;
        const qreal phi = std::acos(2.0 * h2 - 1.0);
        const QVector3D normal(std::sin(phi) * std::cos(theta),
                               std::cos(phi),
                               std::sin(phi) * std::sin(theta));
        QVector3D u(-normal.y(), normal.x(), 0.0f);
        if (u.lengthSquared() < 1e-8f)
            u = QVector3D(1.0f, 0.0f, 0.0f);
        else
            u.normalize();
        const QVector3D v = QVector3D::crossProduct(normal, u);
        const qreal orbitalSpeed = (0.25 + 0.55 * h3) * (h3 > 0.5 ? 1.0 : -1.0);

        for (int point = 0; point < ghostCount; ++point) {
            const qreal angle = (point / qreal(ghostCount)) * 2.0 * kPi;
            const QVector3D projected = project(
                (u * std::cos(angle) + v * std::sin(angle)) * radius,
                yaw, tilt);
            const qreal depth = std::clamp((projected.z() / radius + 1.0) / 2.0,
                                           0.0, 1.0);
            dots.append({center + QPointF(projected.x(), -projected.y()),
                         projected.z(), std::max(0.42, 0.78 * scale),
                         0.48 + depth * 0.18, 0.16 + depth * 0.26});
        }

        for (int particle = 0; particle < particleCount; ++particle) {
            const qreal angle = time * orbitalSpeed
                + (particle / qreal(particleCount)) * 2.0 * kPi + h2 * 6.0;
            const QVector3D projected = project(
                (u * std::cos(angle) + v * std::sin(angle)) * radius,
                yaw, tilt);
            const qreal depth = std::clamp((projected.z() / radius + 1.0) / 2.0,
                                           0.0, 1.0);
            dots.append({center + QPointF(projected.x(), -projected.y()),
                         projected.z(),
                         std::max(0.62, (0.95 + depth * 0.9) * scale),
                         0.58 + depth * 0.42, 0.72 + depth * 0.28});
        }
    }

    std::sort(dots.begin(), dots.end(), [](const OrbDot &left, const OrbDot &right) {
        return left.depth < right.depth;
    });
    for (const OrbDot &dot : std::as_const(dots)) {
        painter->setBrush(shaded(m_tint, dot.brightness, dot.opacity));
        painter->drawEllipse(dot.position, dot.radius, dot.radius);
    }
}
