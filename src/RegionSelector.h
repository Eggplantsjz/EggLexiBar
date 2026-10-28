#pragma once

#include <QRect>
#include <QWidget>

/// Fullscreen translucent overlay that lets the user drag a rectangle on the
/// screen. Emits regionSelected once the mouse is released (or the dialog is
/// cancelled with Esc). All coordinates are in Qt logical screen space.
class RegionSelector : public QWidget
{
    Q_OBJECT
public:
    explicit RegionSelector(QWidget* parent = nullptr);

signals:
    void regionSelected(const QRect& rect);
    void cancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    QPoint m_start;
    QRect m_current;   // drag rectangle in global coords (logical)
};
