#pragma once

#include <QWidget>

#include <utility>
#include <vector>

#include "ai/analysis.h"
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

    // Territory heat map (形势显示): empty points shaded by ownership, dead
    // stones tinted with the opponent's territory color. Pass an empty map to
    // clear the overlay. A copy is kept, so the caller may free its source.
    void setOwnership(const OwnershipMap& ownership);
    void clearOwnership() { ownership_ = OwnershipMap{}; update(); }

    // Variation demo: number each played stone with its step index (1-based),
    // so demo moves are visually distinct from the real game (N2.3).
    void setMoveNumbers(const std::vector<std::pair<Point, int>>& nums);
    void clearMoveNumbers() { move_numbers_.clear(); update(); }

    // D2.4 跳 PV：把引擎的最佳变化（主变化）逐点高亮并编号，与普通高亮区分。
    void setPv(const std::vector<Point>& pts) { pv_ = pts; update(); }
    void clearPv() { pv_.clear(); update(); }

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
    OwnershipMap ownership_;
    std::vector<std::pair<Point, int>> move_numbers_;
    std::vector<Point> pv_;  // D2.4 主变化逐点
};

}  // namespace vgo
