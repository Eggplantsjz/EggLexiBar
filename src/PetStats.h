#pragma once

#include <QWidget>

/// The little pixel "digital pet meter" that sits right next to the eggplant.
///
/// It shows, in a retro LED style:
///   - the number of words the pet ate today (pixel digits),
///   - a segmented fullness bar (today's meals against a daily goal),
///   - the share of today's words that are already familiar (eaten, marked
///     known or retired by the memory curve).
///
/// Everything is painted by hand so the digits share the pixel grid of the
/// eggplant instead of looking like a normal desktop label.
class PetStats : public QWidget
{
    Q_OBJECT
public:
    explicit PetStats(QWidget* parent = nullptr);

    void setStats(int eaten, int familiar, int seen, int goal);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void paintDigits(QPainter& painter, const QPoint& origin, int value) const;
    void paintGauge(QPainter& painter, const QRect& rect, qreal fullness) const;

    int m_eaten = 0;
    int m_familiar = 0;
    int m_seen = 0;
    int m_goal = 20;
};
