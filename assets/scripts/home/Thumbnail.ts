import { Color, Graphics, Node } from 'cc';
import { LevelPreview, RingPreview } from '../bridge/GameBridge';
import { block, node, rounded } from '../view/UiKit';
import { paletteColor } from '../view/SpriteFactory';

const RING_FLOOR = [new Color(30, 38, 58, 255), new Color(34, 44, 66, 255)];

/**
 * 关卡缩略图（任务书 7.4.1）：根据 GET /levels 返回的预览数据绘制，
 * 从不使用截图，因此任何关卡修改都会自动显示在主页面
 * 上，缩略图同时可作为“关卡目录与关卡文件一致”的
 * 证据。
 *
 * 曲线关卡对道路、水果和可选的扇形环地面使用同一套世界空间
 * 变换；网格关卡保留第二阶段的单元格布局。
 */
export class Thumbnail {
    readonly root: Node;

    constructor(parent: Node, preview: LevelPreview, size: number) {
        this.root = node(parent, 'Thumbnail', size, size);
        const polyline = preview.polyline ?? [];
        if (polyline.length >= 2) {
            this.drawCurve(preview, size);
            return;
        }
        const cols = Math.max(1, preview.cols);
        const rows = Math.max(1, preview.rows);
        const cell = Math.min(size / cols, size / rows);
        const originX = -((cols - 1) * cell) / 2;
        // Cocos 的 y 轴向上增长，因此第 0 行位于方框顶部。
        const originY = ((rows - 1) * cell) / 2;
        const place = (r: number, c: number): { x: number; y: number } => ({
            x: originX + c * cell,
            y: originY - r * cell,
        });

        for (const [r, c] of preview.road ?? []) {
            const point = place(r, c);
            const tile = block(this.root, `Road_${r}_${c}`, cell, cell, new Color(59, 66, 80, 255));
            tile.setPosition(point.x, point.y, 0);
        }
        for (const [r, c] of preview.island ?? []) {
            const point = place(r, c);
            const tile = block(this.root, `Island_${r}_${c}`, cell, cell, new Color(62, 54, 44, 255));
            tile.setPosition(point.x, point.y, 0);
        }
        for (const [r, c, colorId] of preview.fruits ?? []) {
            const point = place(r, c);
            const tile = rounded(this.root, `Fruit_${r}_${c}`, cell * 0.86, cell * 0.86,
                paletteColor(colorId), Math.max(1, cell * 0.22));
            tile.setPosition(point.x, point.y, 0);
        }
    }

    private drawCurve(preview: LevelPreview, size: number): void {
        const polyline = preview.polyline ?? [];
        let minX = Number.POSITIVE_INFINITY;
        let maxX = Number.NEGATIVE_INFINITY;
        let minY = Number.POSITIVE_INFINITY;
        let maxY = Number.NEGATIVE_INFINITY;
        const consider = (x: number, y: number): void => {
            minX = Math.min(minX, x);
            maxX = Math.max(maxX, x);
            minY = Math.min(minY, y);
            maxY = Math.max(maxY, y);
        };
        for (const [x, y] of polyline) {
            consider(x, y);
        }
        for (const [r, c] of preview.fruits ?? []) {
            consider(c, r);
        }
        const ring = preview.ring;
        if (ring) {
            consider(ring.centre[0] - ring.outerRadius, ring.centre[1] - ring.outerRadius);
            consider(ring.centre[0] + ring.outerRadius, ring.centre[1] + ring.outerRadius);
        }
        if (!Number.isFinite(minX) || !Number.isFinite(minY)) {
            return;
        }
        const spanX = Math.max(1, maxX - minX);
        const spanY = Math.max(1, maxY - minY);
        const scale = Math.min((size * 0.92) / spanX, (size * 0.92) / spanY);
        const originX = -((minX + maxX) / 2) * scale;
        const originY = ((minY + maxY) / 2) * scale;
        const canvasX = (x: number): number => originX + x * scale;
        const canvasY = (y: number): number => originY - y * scale;

        this.drawRingFloor(ring, scale, originX, originY, size);

        const path = node(this.root, 'RoadPath', size, size);
        const graphics = path.addComponent(Graphics);
        graphics.lineWidth = Math.max(2, scale);
        graphics.strokeColor = new Color(59, 66, 80, 255);
        graphics.lineJoin = Graphics.LineJoin.ROUND;
        graphics.lineCap = Graphics.LineCap.ROUND;
        graphics.moveTo(canvasX(polyline[0][0]), canvasY(polyline[0][1]));
        for (let i = 1; i < polyline.length; i += 1) {
            graphics.lineTo(canvasX(polyline[i][0]), canvasY(polyline[i][1]));
        }
        graphics.stroke();

        const tile = Math.max(2, scale * 0.86);
        for (const [r, c, colorId] of preview.fruits ?? []) {
            const fruit = rounded(this.root, `Fruit_${r}_${c}`, tile, tile, paletteColor(colorId),
                Math.max(1, tile * 0.24));
            fruit.setPosition(canvasX(c), canvasY(r), 0);
        }
    }

    private drawRingFloor(ring: RingPreview | undefined, scale: number, originX: number,
                          originY: number, size: number): void {
        if (!ring || ring.sectors <= 0 || !(ring.outerRadius > ring.innerRadius)) {
            return;
        }
        const holder = node(this.root, 'SectorRing', size, size);
        const graphics = holder.addComponent(Graphics);
        const sectorDeg = ring.sweepDeg / ring.sectors;
        const gap = Math.max(0, Math.min(ring.gapDeg, sectorDeg * 0.9));
        const [cx, cy] = ring.centre;
        const point = (radius: number, deg: number): { x: number; y: number } => {
            const radians = (deg * Math.PI) / 180;
            return {
                x: originX + (cx + radius * Math.cos(radians)) * scale,
                y: originY - (cy + radius * Math.sin(radians)) * scale,
            };
        };
        const steps = 8;
        for (let s = 0; s < ring.sectors; s += 1) {
            const from = ring.startDeg + s * sectorDeg + gap / 2;
            const to = ring.startDeg + (s + 1) * sectorDeg - gap / 2;
            if (!(to > from)) {
                continue;
            }
            graphics.fillColor = RING_FLOOR[s % RING_FLOOR.length];
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
}
