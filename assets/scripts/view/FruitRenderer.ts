import { Color, Node, Vec3 } from 'cc';
import { GridCell, Snapshot, SnapshotFruit } from '../bridge/GameBridge';
import { ANIM, flyTo, pulse, tweenScale } from './Anims';
import { BoardLayout } from './BoardLayout';
import { addRoundBlock, paletteColor, setFruitColor, uiNode } from './SpriteFactory';

interface FruitEntry {
    node: Node;
    size: number;
}

/**
 * 绘制水果方块（任务书 7 T-21）。
 *
 * 曲线关卡使用快照中稳定的 `fruitId` 作为视图键，因此
 * 补充时同一个 node 会向内滑动，被收集时会飞入车内。
 * 网格关卡回退到单元格键（`r_c`），这也是第 1/2 阶段
 * 截图和一致性探针所期望的。
 *
 * `sync()` 是唯一读取快照的地方；这里的全部动画辅助
 * 函数都仅用于展示（任务书 5.7 A-1/A-2）。
 */
export class FruitRenderer {
    private readonly entries = new Map<string, FruitEntry>();
    private readonly positions = new Map<string, Vec3>();
    private mode: 'grid' | 'curves' = 'grid';

    constructor(private readonly layer: Node, private readonly layout: BoardLayout) {}

    sync(snapshot: Snapshot, layout: BoardLayout): void {
        this.mode = (snapshot.world ?? 'grid') === 'curves' ? 'curves' : 'grid';
        if (this.mode === 'curves') {
            this.syncCurve(snapshot, layout);
        } else {
            this.syncGrid(snapshot, layout);
        }
    }

    private tileSize(scale: number): number {
        const base = this.layout.cellSize * (scale > 0 ? scale : 1) * 0.94;
        return Math.max(8, base);
    }

    private syncCurve(snapshot: Snapshot, layout: BoardLayout): void {
        const seen = new Set<string>();
        for (const fruit of snapshot.fruits ?? []) {
            const key = this.keyOf(fruit);
            seen.add(key);
            const size = this.tileSize(fruit.size);
            const position = layout.positionOfWorld(fruit.x, fruit.y);
            let entry = this.entries.get(key);
            if (!entry) {
                const node = addRoundBlock(this.layer, `Fruit_${fruit.id}`, size, size,
                    paletteColor(fruit.fruit), Math.max(4, size * 0.24), true);
                node.setPosition(position);
                entry = { node, size };
                this.entries.set(key, entry);
            }
            const base = paletteColor(fruit.fruit);
            const tint = fruit.settle > 0
                ? new Color(Math.round(base.r * 0.65), Math.round(base.g * 0.65),
                    Math.round(base.b * 0.65), 255)
                : base;
            setFruitColor(entry.node, tint, Math.max(4, size * 0.24));
            if (Math.abs(entry.size - size) > 0.5) {
                this.setNodeSize(entry.node, size);
                tweenScale(entry.node, 1, ANIM.scale);
                entry.size = size;
            }
            const previous = this.positions.get(key);
            this.positions.set(key, position.clone());
            if (previous && Vec3.distance(previous, position) > 0.5) {
                // 在槽位之间移动的水果（补充）：保留 node，
                // EventDirector 会在本次同步后启动滑动动画。
                entry.node.setPosition(position);
            } else {
                entry.node.setPosition(position);
            }
        }
        for (const [key, entry] of this.entries) {
            if (!seen.has(key)) {
                // 被收集的水果由 EventDirector 播放动画移出；如果它
                // 没有接管该 node，则立即移除。
                entry.node.destroy();
                this.entries.delete(key);
                this.positions.delete(key);
            }
        }
    }

    private syncGrid(snapshot: Snapshot, layout: BoardLayout): void {
        const seen = new Set<string>();
        const rows = snapshot.grid.rows;
        const cols = snapshot.grid.cols;
        for (let r = 0; r < rows; r += 1) {
            for (let c = 0; c < cols; c += 1) {
                const cell: GridCell | null = snapshot.grid.cells[r]?.[c] ?? null;
                if (!cell) {
                    continue;
                }
                const key = `${r}_${c}`;
                seen.add(key);
                const size = this.tileSize(cell.size);
                const position = layout.positionOf(r, c);
                let entry = this.entries.get(key);
                if (!entry) {
                    const node = addRoundBlock(this.layer, `Fruit_${r}_${c}`, size, size,
                        paletteColor(cell.fruit), Math.max(4, size * 0.24), true);
                    node.setPosition(position);
                    entry = { node, size };
                    this.entries.set(key, entry);
                }
                const base = paletteColor(cell.fruit);
                const tint = cell.settle > 0
                    ? new Color(Math.round(base.r * 0.65), Math.round(base.g * 0.65),
                        Math.round(base.b * 0.65), 255)
                    : base;
                setFruitColor(entry.node, tint, Math.max(4, size * 0.24));
                if (Math.abs(entry.size - size) > 0.5) {
                    this.setNodeSize(entry.node, size);
                    tweenScale(entry.node, 1, ANIM.scale);
                    entry.size = size;
                }
                entry.node.setPosition(position);
                this.positions.set(key, position.clone());
            }
        }
        for (const [key, entry] of this.entries) {
            if (!seen.has(key)) {
                entry.node.destroy();
                this.entries.delete(key);
                this.positions.delete(key);
            }
        }
    }

    private keyOf(fruit: SnapshotFruit): string {
        return `id:${fruit.id}`;
    }

    /** 曲线水果 id 的 node 键。 */
    static keyForFruit(id: number): string {
        return `id:${id}`;
    }

    /** 网格单元格的 node 键。 */
    static keyForCell(cell: [number, number]): string {
        return `${cell[0]}_${cell[1]}`;
    }

    private setNodeSize(node: Node, size: number): void {
        const transform = node.getComponent('cc.UITransform') as unknown as
            { setContentSize: (w: number, h: number) => void } | null;
        if (transform) {
            transform.setContentSize(size, size);
        }
    }

    nodeOf(key: string): Node | null {
        return this.entries.get(key)?.node ?? null;
    }

    /** 移除视图但不销毁它，以便调用方可以为其播放动画。 */
    detach(key: string): Node | null {
        const entry = this.entries.get(key);
        if (!entry) {
            return null;
        }
        this.entries.delete(key);
        this.positions.delete(key);
        return entry.node;
    }

    /** 该 node 最后一次绘制时的位置（canvas 空间）。 */
    lastPosition(key: string): Vec3 | null {
        return this.positions.get(key) ?? null;
    }

    /** 收集动画：先脉冲，再飞入车内并消失。 */
    playCollect(key: string, target: Vec3, onDone?: () => void): boolean {
        const node = this.detach(key);
        if (!node) {
            return false;
        }
        pulse(node, 1.15, ANIM.collectPulse);
        flyTo(node, target, ANIM.collectFly, () => {
            if (node.isValid) {
                node.destroy();
            }
            onDone?.();
        });
        return true;
    }

    /** 补充动画：从源槽位滑动到目标槽位。 */
    playRefill(key: string, from: Vec3): boolean {
        const entry = this.entries.get(key);
        if (!entry) {
            return false;
        }
        const to = entry.node.position.clone();
        entry.node.setPosition(from);
        flyTo(entry.node, to, ANIM.collectFly);
        return true;
    }

    nodeCount(): number {
        return this.entries.size;
    }

    /** 创建用于“收集”闪光效果的浮动副本。 */
    spawnGhost(key: string, position: Vec3, color: Color): Node {
        const size = this.layout.cellSize * 0.8;
        const node = addRoundBlock(this.layer, `Ghost_${key}`, size, size, color, size * 0.24, true);
        node.setPosition(position);
        return node;
    }
}

export function createFruitLayer(parent: Node): Node {
    return uiNode(parent, 'FruitLayer', 720, 1280);
}
