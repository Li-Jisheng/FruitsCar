import { Vec3 } from 'cc';
import { DESIGN_HEIGHT, DESIGN_WIDTH, px, py } from './UiKit';

export interface Rect {
    left: number;
    right: number;
    bottom: number;
    top: number;
}

export interface BoardBounds {
    minRow: number;
    maxRow: number;
    minCol: number;
    maxCol: number;
}

/**
 * 将关卡坐标映射到 720x1280 的设计画布上。
 *
 * 两种坐标模式共用同一套投影：
 *   * 网格关卡 - 整数单元格 (row, col)，与第 1/2 阶段相同；
 *   * 曲线关卡 - 自由世界坐标点 (x = col, y = row)，任务书 5.3。
 *
 * 棋盘会缩放以适配绘制区域，因此纵向、宽幅、弯折和
 * 曲线棋盘都能保持可见，无需为每个关卡编写布局代码。
 */
export class BoardLayout {
    readonly scale: number;
    readonly cell: number;
    readonly mode: 'grid' | 'curves';
    private readonly bounds: BoardBounds;
    private readonly centreC: number;
    private readonly centreR: number;
    private readonly centreX: number;
    private readonly centreY: number;

    constructor(bounds: BoardBounds, area: Rect, cellSize = 56, mode: 'grid' | 'curves' = 'grid') {
        this.bounds = bounds;
        this.mode = mode;
        this.cell = cellSize;
        this.centreC = (bounds.minCol + bounds.maxCol) / 2;
        this.centreR = (bounds.minRow + bounds.maxRow) / 2;
        const width = Math.max(1, bounds.maxCol - bounds.minCol + 1) * this.cell;
        const height = Math.max(1, bounds.maxRow - bounds.minRow + 1) * this.cell;
        const areaWidth = area.right - area.left;
        // 该矩形使用任务书坐标（左上角为原点，y 轴向下），因此
        // 底边数值大于顶边。
        const areaHeight = area.bottom - area.top;
        // 矩形以任务书坐标给出（左上角为原点，y 轴向下）；
        // 在同一空间中计算，仅在生成位置时转换。
        const fit = Math.max(0.2, Math.min(areaWidth / width, areaHeight / height, 1.25));
        this.scale = Math.max(0.3, fit);
        this.centreX = (area.left + area.right) / 2;
        this.centreY = (area.top + area.bottom) / 2;
    }

    /** 网格模式：行/列，加上可能超出边界的道路单元格。 */
    static forGrid(rows: number, cols: number, roadCells: [number, number][], area: Rect,
                   cellSize = 56): BoardLayout {
        let minRow = 0;
        let maxRow = rows - 1;
        let minCol = 0;
        let maxCol = cols - 1;
        for (const [r, c] of roadCells) {
            if (r < minRow) { minRow = r; }
            if (r > maxRow) { maxRow = r; }
            if (c < minCol) { minCol = c; }
            if (c > maxCol) { maxCol = c; }
        }
        return new BoardLayout({ minRow, maxRow, minCol, maxCol }, area, cellSize, 'grid');
    }

    /** 曲线模式：边界由采样道路和每个水果共同确定。 */
    static forCurve(polyline: [number, number][], fruits: { x: number; y: number }[], area: Rect,
                    cellSize = 56): BoardLayout {
        let minRow = Number.POSITIVE_INFINITY;
        let maxRow = Number.NEGATIVE_INFINITY;
        let minCol = Number.POSITIVE_INFINITY;
        let maxCol = Number.NEGATIVE_INFINITY;
        const consider = (x: number, y: number): void => {
            if (x < minCol) { minCol = x; }
            if (x > maxCol) { maxCol = x; }
            if (y < minRow) { minRow = y; }
            if (y > maxRow) { maxRow = y; }
        };
        for (const [x, y] of polyline) {
            consider(x, y);
        }
        for (const fruit of fruits) {
            consider(fruit.x, fruit.y);
        }
        if (!Number.isFinite(minRow)) {
            minRow = 0;
            maxRow = 1;
            minCol = 0;
            maxCol = 1;
        }
        const pad = 0.8;
        return new BoardLayout({ minRow: minRow - pad, maxRow: maxRow + pad, minCol: minCol - pad,
            maxCol: maxCol + pad }, area, cellSize, 'curves');
    }

    get cellSize(): number {
        return this.cell * this.scale;
    }

    get canvasWidth(): number {
        return DESIGN_WIDTH;
    }

    get canvasHeight(): number {
        return DESIGN_HEIGHT;
    }

    /** 网格中心在 canvas 坐标中的位置（row, col）。 */
    positionOf(r: number, c: number): Vec3 {
        const x = this.centreX + (c - this.centreC) * this.cell * this.scale;
        const y = this.centreY + (r - this.centreR) * this.cell * this.scale;
        return new Vec3(px(x), py(y), 0);
    }

    /** 自由世界坐标点（x = col，y = row）在 canvas 坐标中的位置。 */
    positionOfWorld(x: number, y: number): Vec3 {
        return this.positionOf(y, x);
    }

    /**
     * 世界坐标对应的原始 canvas 空间点，供在零位置 node
     * 内绘制的 Graphics 路径使用（曲线道路描边）。
     */
    canvasPoint(x: number, y: number): { x: number; y: number } {
        const designX = this.centreX + (x - this.centreC) * this.cell * this.scale;
        const designY = this.centreY + (y - this.centreR) * this.cell * this.scale;
        return { x: px(designX), y: py(designY) };
    }

    /** 小数网格点（保留给第二阶段的车道插值调用方）。 */
    positionOfFloat(r: number, c: number): Vec3 {
        return this.positionOf(r, c);
    }

    /** 只读布局诊断信息（客户端报告 / 证据）。 */
    debugInfo(): Record<string, unknown> {
        return {
            mode: this.mode,
            scale: this.scale,
            cell: this.cell,
            cellSize: this.cellSize,
            bounds: this.bounds,
            centreX: this.centreX,
            centreY: this.centreY,
            centreR: this.centreR,
            centreC: this.centreC,
        };
    }
}
