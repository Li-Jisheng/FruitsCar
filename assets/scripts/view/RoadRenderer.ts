import { Color, Graphics, Node } from 'cc';
import { LevelSummary, RingPreview, Snapshot } from '../bridge/GameBridge';
import { BoardLayout } from './BoardLayout';
import { addBlock, uiNode } from './SpriteFactory';
import { triangle } from './UiKit';

const TARMAC = new Color(59, 66, 80, 255);
const TARMAC_EDGE = new Color(74, 82, 99, 255);
const ISLAND = new Color(62, 54, 44, 255);
const ARROW = new Color(245, 248, 255, 235);
// 半扇形环地面：每个扇区交替使用两种深蓝色调，使五个
// 楔形区域即使在水果堆密集处也保持清晰可辨。
const SECTOR_FLOOR = [new Color(30, 38, 58, 255), new Color(34, 44, 66, 255)];

/**
 * 绘制可行驶表面（任务书 7 T-20）。
 *
 * 网格关卡保留第一阶段的逐单元格柏油方块（node 名为 `Road_i`，
 * 一致性探针会计数）。曲线关卡绘制与逻辑使用的 *同一* 条
 * 采样折线——一条使用圆角连接的 `moveTo/lineTo` 路径——因此
 * 画面与碰撞几何不会出现分歧（任务书 4.1）。
 */
export class RoadRenderer {
    readonly node: Node;
    private readonly tarmac: Node;
    private readonly decorations: Node;
    private key = '';
    private trackNodes = 0;

    constructor(parent: Node) {
        this.node = uiNode(parent, 'RoadRoot', 720, 1280);
        this.tarmac = uiNode(this.node, 'Tarmac', 720, 1280);
        this.decorations = uiNode(this.node, 'RoadDecor', 720, 1280);
    }

    sync(snapshot: Snapshot, layout: BoardLayout, level: LevelSummary | null): void {
        const curve = (snapshot.world ?? 'grid') === 'curves';
        const polyline = snapshot.road.polyline ?? [];
        const key = [
            snapshot.levelId, snapshot.levelEpoch, curve ? 'curves' : 'grid',
            curve ? polyline.length : (snapshot.road.track ?? snapshot.road.cells).length,
            layout.mode, layout.cellSize.toFixed(2),
        ].join(':');
        if (key === this.key) {
            return;
        }
        this.key = key;
        this.tarmac.removeAllChildren();
        this.decorations.removeAllChildren();
        this.trackNodes = 0;

        if (curve) {
            this.drawPolyline(polyline, layout, snapshot.road.width);
        } else {
            const track = snapshot.road.track ?? snapshot.road.cells;
            for (let i = 0; i < track.length; i += 1) {
                const [r, c] = track[i];
                const cell = addBlock(this.tarmac, `Road_${i}`, layout.cellSize + 0.5,
                    layout.cellSize + 0.5, TARMAC);
                cell.setPosition(layout.positionOf(r, c));
            }
            this.trackNodes = track.length;
        }

        // 半扇形环地面（五个楔形之间有狭窄间隙）：
        // 在柏油之后绘制，但位于车道之外、水果层之下。
        this.drawSectorRing(level?.preview?.ring, layout);

        const island = level?.preview?.island ?? [];
        for (let i = 0; i < island.length; i += 1) {
            const [r, c] = island[i];
            const cell = addBlock(this.decorations, `Island_${i}`, layout.cellSize - 1.5,
                layout.cellSize - 1.5, ISLAND);
            cell.setPosition(layout.positionOf(r, c));
        }

        this.drawArrows(snapshot, layout, curve, polyline);
    }

    private drawPolyline(polyline: [number, number][], layout: BoardLayout, width: number): void {
        if (polyline.length < 2) {
            return;
        }
        const path = uiNode(this.tarmac, 'Road_Path', 720, 1280);
        const graphics = path.addComponent(Graphics);
        const lineWidth = Math.max(10, layout.cellSize * Math.max(1, width));
        // 柏油下方的柔和路肩：进一步遮住折线的拐角。
        graphics.lineWidth = lineWidth + 8;
        graphics.strokeColor = TARMAC_EDGE;
        graphics.lineJoin = Graphics.LineJoin.ROUND;
        graphics.lineCap = Graphics.LineCap.ROUND;
        graphics.moveTo(layout.canvasPoint(polyline[0][0], polyline[0][1]).x,
            layout.canvasPoint(polyline[0][0], polyline[0][1]).y);
        for (let i = 1; i < polyline.length; i += 1) {
            const point = layout.canvasPoint(polyline[i][0], polyline[i][1]);
            graphics.lineTo(point.x, point.y);
        }
        graphics.stroke();

        graphics.lineWidth = lineWidth;
        graphics.strokeColor = TARMAC;
        graphics.moveTo(layout.canvasPoint(polyline[0][0], polyline[0][1]).x,
            layout.canvasPoint(polyline[0][0], polyline[0][1]).y);
        for (let i = 1; i < polyline.length; i += 1) {
            const point = layout.canvasPoint(polyline[i][0], polyline[i][1]);
            graphics.lineTo(point.x, point.y);
        }
        graphics.stroke();
        // 整条曲线是一个 node；将其报告为一个柏油 node。
        this.trackNodes = 1;
    }

    /**
     * 将扇形环水果区域的半环形地面绘制为 `sectors` 个
     * 填充楔形。`presentation.ring` 来自关卡数据，因此
     * 扇区数量、半径和间隙都不在视图中硬编码。
     */
    private drawSectorRing(ring: RingPreview | undefined, layout: BoardLayout): void {
        if (!ring || ring.sectors <= 0 || !(ring.outerRadius > ring.innerRadius) || !(ring.sweepDeg > 0)) {
            return;
        }
        const sectorDeg = ring.sweepDeg / ring.sectors;
        const gap = Math.max(0, Math.min(ring.gapDeg, sectorDeg * 0.9));
        const holder = uiNode(this.decorations, 'SectorRing', 720, 1280);
        const graphics = holder.addComponent(Graphics);
        const [cx, cy] = ring.centre;
        const steps = 10;
        const point = (radius: number, deg: number): { x: number; y: number } => {
            const radians = (deg * Math.PI) / 180;
            return layout.canvasPoint(cx + radius * Math.cos(radians), cy + radius * Math.sin(radians));
        };
        for (let s = 0; s < ring.sectors; s += 1) {
            const from = ring.startDeg + s * sectorDeg + gap / 2;
            const to = ring.startDeg + (s + 1) * sectorDeg - gap / 2;
            if (!(to > from)) {
                continue;
            }
            graphics.fillColor = SECTOR_FLOOR[s % SECTOR_FLOOR.length];
            const start = point(ring.innerRadius, from);
            graphics.moveTo(start.x, start.y);
            for (let i = 1; i <= steps; i += 1) {
                const inner = point(ring.innerRadius, from + ((to - from) * i) / steps);
                graphics.lineTo(inner.x, inner.y);
            }
            for (let i = steps; i >= 0; i -= 1) {
                const outer = point(ring.outerRadius, from + ((to - from) * i) / steps);
                graphics.lineTo(outer.x, outer.y);
            }
            graphics.close();
            graphics.fill();
        }
    }

    private drawArrows(snapshot: Snapshot, layout: BoardLayout, curve: boolean,
                       polyline: [number, number][]): void {
        let entry: { x: number; y: number } | null = null;
        let exit: { x: number; y: number } | null = null;
        let entryAngle = 180;
        let exitAngle = 180;
        if (curve && polyline.length >= 2) {
            const first = layout.canvasPoint(polyline[0][0], polyline[0][1]);
            const second = layout.canvasPoint(polyline[1][0], polyline[1][1]);
            const last = layout.canvasPoint(polyline[polyline.length - 1][0],
                polyline[polyline.length - 1][1]);
            const before = layout.canvasPoint(polyline[polyline.length - 2][0],
                polyline[polyline.length - 2][1]);
            entry = first;
            exit = last;
            entryAngle = Math.atan2(first.y - second.y, first.x - second.x) * 180 / Math.PI + 90;
            exitAngle = Math.atan2(last.y - before.y, last.x - before.x) * 180 / Math.PI + 90;
        } else if (!curve && snapshot.road.cells.length >= 2) {
            const entryCell = snapshot.road.cells[0];
            const exitCell = snapshot.road.cells[snapshot.road.cells.length - 1];
            const entryPos = layout.positionOf(entryCell[0] + 1.0, entryCell[1]);
            const exitPos = layout.positionOf(exitCell[0] + 1.0, exitCell[1]);
            entry = { x: entryPos.x, y: entryPos.y };
            exit = { x: exitPos.x, y: exitPos.y };
        }
        if (entry) {
            const node = triangle(this.decorations, 'ArrowEntry', 24, ARROW, 180);
            node.setPosition(entry.x, entry.y + 10, 0);
            node.angle = -entryAngle;
        }
        if (exit) {
            const node = triangle(this.decorations, 'ArrowExit', 24, ARROW, 180);
            node.setPosition(exit.x, exit.y + 10, 0);
            node.angle = -exitAngle;
        }
    }

    /** 柏油 node 数量（曲线：1 条路径；网格：每个单元格一个）。 */
    count(): number {
        return this.trackNodes;
    }
}
