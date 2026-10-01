#include "GobanWidget.h"

#include <QLineF>
#include <QMouseEvent>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace vgo {

namespace {
QString columnLabel(int x) {
    // A..T, skipping 'I' (standard Go coordinate).
    int c = x + 'A';
    if (c >= 'I') ++c;
    return QString(QChar(c));
}
}  // namespace

GobanWidget::GobanWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(420, 420);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void GobanWidget::setBoard(const Board* board) {
    board_ = board;
    has_last_ = false;
    has_hover_ = false;
    update();
}

void GobanWidget::setLastMove(const Point& p, bool has) {
    last_move_ = p;
    has_last_ = has;
    update();
}

void GobanWidget::setHover(const Point& p, bool has) {
    hover_ = p;
    has_hover_ = has;
    update();
}

void GobanWidget::setCandidates(const std::vector<Point>& pts) {
    candidates_ = pts;
    update();
}

void GobanWidget::setHighlight(const std::vector<Point>& pts) {
    highlight_ = pts;
    update();
}

double GobanWidget::cellSize() const {
    const int s = board_ ? board_->size() : 9;
    return std::min(width(), height()) / (s + 1.6);
}

QPointF GobanWidget::intersectionToPixel(int x, int y) const {
    const int s = board_ ? board_->size() : 9;
    const double cell = cellSize();
    const double ox = (width() - (s - 1) * cell) / 2.0;
    const double oy = (height() - (s - 1) * cell) / 2.0;
    return QPointF(ox + x * cell, oy + y * cell);
}

Point GobanWidget::pixelToIntersection(const QPointF& pos) const {
    const int s = board_ ? board_->size() : 9;
    const double cell = cellSize();
    const double ox = (width() - (s - 1) * cell) / 2.0;
    const double oy = (height() - (s - 1) * cell) / 2.0;
    const int x = static_cast<int>(std::lround((pos.x() - ox) / cell));
    const int y = static_cast<int>(std::lround((pos.y() - oy) / cell));
    return {x, y};
}

bool GobanWidget::isStarPoint(int size, int x, int y) {
    static const Point pts9[] = {{2, 2}, {2, 6}, {6, 2}, {6, 6}, {4, 4}};
    static const Point pts13[] = {{3, 3}, {3, 9}, {9, 3}, {9, 9}, {6, 6}};
    static const Point pts19[] = {{3, 3}, {3, 9},  {3, 15}, {9, 3},  {9, 9},
                                  {9, 15}, {15, 3}, {15, 9}, {15, 15}};

    const Point* pts = nullptr;
    int n = 0;
    if (size == 9) {
        pts = pts9;
        n = 5;
    } else if (size == 13) {
        pts = pts13;
        n = 5;
    } else if (size == 19) {
        pts = pts19;
        n = 9;
    }
    for (int i = 0; i < n; ++i) {
        if (pts[i].x == x && pts[i].y == y) return true;
    }
    return false;
}

void GobanWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const int s = board_ ? board_->size() : 9;
    const double cell = cellSize();
    const double stoneR = cell * 0.46;

    p.fillRect(rect(), QColor("#DCB35C"));

    // Grid lines.
    QPen linePen(QColor("#3A2A18"), std::max(1.0, cell * 0.045));
    p.setPen(linePen);
    for (int i = 0; i < s; ++i) {
        p.drawLine(intersectionToPixel(i, 0), intersectionToPixel(i, s - 1));
        p.drawLine(intersectionToPixel(0, i), intersectionToPixel(s - 1, i));
    }

    // Star points.
    if (board_) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#3A2A18"));
        const double r = cell * 0.11;
        for (int y = 0; y < s; ++y) {
            for (int x = 0; x < s; ++x) {
                if (isStarPoint(s, x, y)) {
                    const QPointF c = intersectionToPixel(x, y);
                    p.drawEllipse(c, r, r);
                }
            }
        }
    }

    // Stones.
    for (int y = 0; y < s; ++y) {
        for (int x = 0; x < s; ++x) {
            if (!board_ || board_->at(x, y) == Color::Empty) continue;
            const QPointF c = intersectionToPixel(x, y);
            if (board_->at(x, y) == Color::Black) {
                p.setBrush(QColor("#1C1C1C"));
                p.setPen(QPen(QColor("#000000"), 1));
            } else {
                p.setBrush(QColor("#F7F7F2"));
                p.setPen(QPen(QColor("#8A8A82"), 1));
            }
            p.drawEllipse(c, stoneR, stoneR);
        }
    }

    // Last move marker.
    if (has_last_ && board_ && board_->in_bounds(last_move_.x, last_move_.y)) {
        const QPointF c = intersectionToPixel(last_move_.x, last_move_.y);
        const QColor mark = (board_->at(last_move_.x, last_move_.y) == Color::Black)
                                ? QColor("#FFFFFF")
                                : QColor("#000000");
        p.setPen(Qt::NoPen);
        p.setBrush(mark);
        p.drawEllipse(c, cell * 0.13, cell * 0.13);
    }

    // Hover ghost stone.
    if (has_hover_ && board_ && board_->in_bounds(hover_.x, hover_.y) &&
        board_->at(hover_.x, hover_.y) == Color::Empty) {
        const QPointF c = intersectionToPixel(hover_.x, hover_.y);
        const QColor g = (ghost_color_ == Color::Black)
                             ? QColor(0, 0, 0, 90)
                             : QColor(255, 255, 255, 120);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawEllipse(c, stoneR, stoneR);
    }

    // Candidate move markers (small dots).
    for (const Point& pt : candidates_) {
        if (!board_ || !board_->in_bounds(pt.x, pt.y) ||
            board_->at(pt.x, pt.y) != Color::Empty)
            continue;
        const QPointF c = intersectionToPixel(pt.x, pt.y);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(200, 60, 40, 200));
        p.drawEllipse(c, cell * 0.13, cell * 0.13);
    }

    // Highlight rings (points referenced by an answer).
    for (const Point& pt : highlight_) {
        if (!board_ || !board_->in_bounds(pt.x, pt.y)) continue;
        const QPointF c = intersectionToPixel(pt.x, pt.y);
        p.setPen(QPen(QColor(30, 120, 210), std::max(1.5, cell * 0.06)));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, cell * 0.30, cell * 0.30);
    }

    // Coordinate labels.
    QFont f = p.font();
    f.setPixelSize(std::max(8, static_cast<int>(cell * 0.42)));
    p.setFont(f);
    p.setPen(QColor("#3A2A18"));
    for (int i = 0; i < s; ++i) {
        const QPointF top = intersectionToPixel(i, 0);
        p.drawText(QRectF(top.x() - cell / 2, top.y() - cell * 1.35, cell, cell),
                   Qt::AlignCenter, columnLabel(i));
        const QPointF left = intersectionToPixel(0, i);
        p.drawText(QRectF(left.x() - cell * 1.6, left.y() - cell / 2, cell, cell),
                   Qt::AlignCenter, QString::number(s - i));
    }
}

void GobanWidget::mousePressEvent(QMouseEvent* event) {
    if (!board_ || event->button() != Qt::LeftButton) return;
    const Point pt = pixelToIntersection(event->pos());
    if (board_->in_bounds(pt.x, pt.y)) {
        const double d = QLineF(intersectionToPixel(pt.x, pt.y), event->pos()).length();
        if (d <= cellSize() * 0.5) {
            emit pointClicked(pt.x, pt.y);
        }
    }
}

void GobanWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!board_) {
        setHover({-1, -1}, false);
        return;
    }
    const Point pt = pixelToIntersection(event->pos());
    if (board_->in_bounds(pt.x, pt.y)) {
        const double d = QLineF(intersectionToPixel(pt.x, pt.y), event->pos()).length();
        if (d <= cellSize() * 0.5) {
            setHover(pt, true);
            return;
        }
    }
    setHover({-1, -1}, false);
}

void GobanWidget::leaveEvent(QEvent*) {
    setHover({-1, -1}, false);
}

}  // namespace vgo
