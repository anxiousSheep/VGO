#pragma once

#include <QWidget>

#include "go/board.h"
#include "go/go_types.h"

namespace vgo {

// Paints a Go board and translates mouse clicks into intersection coordinates.
// Purely a view: it holds no Game, only a Board pointer plus rendering hints.
class GobanWidget : public QWidget {
    Q_OBJECT

public:
    explicit GobanWidget(QWidget* parent = nullptr);

    void setBoard(const Board* board);
    void setLastMove(const Point& p, bool has);
    void setGhostColor(Color c) { ghost_color_ = c; }
    void setHover(const Point& p, bool has);
    void setCandidates(const std::vector<Point>& pts);
    void setHighlight(const std::vector<Point>& pts);

signals:
    void pointClicked(int x, int y);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    double cellSize() const;
    QPointF intersectionToPixel(int x, int y) const;
    Point pixelToIntersection(const QPointF& pos) const;
    static bool isStarPoint(int size, int x, int y);

    const Board* board_ = nullptr;
    Point last_move_{-1, -1};
    bool has_last_ = false;
    Point hover_{-1, -1};
    bool has_hover_ = false;
    Color ghost_color_ = Color::Black;
    std::vector<Point> candidates_;
    std::vector<Point> highlight_;
};

}  // namespace vgo
