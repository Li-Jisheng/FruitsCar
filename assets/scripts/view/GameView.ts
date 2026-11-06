import { Color, Director, Label, Node, Vec3, director, game } from 'cc';
import { GameBridge, LevelSummary, Snapshot } from '../bridge/GameBridge';
import { InputController } from '../input/InputController';
import { BoardLayout } from './BoardLayout';
import { CarRenderer } from './CarRenderer';
import { EventDirector } from './EventDirector';
import { FruitRenderer } from './FruitRenderer';
import { Hud } from './Hud';
import { ItemBar } from './ItemBar';
import { RoadRenderer } from './RoadRenderer';
import { addBlock, uiNode } from './SpriteFactory';

export interface GameViewCallbacks {
    onBack: () => void;
    onRestart: () => void;
    onHome: () => void;
    onFeedback: (message: string, accepted: boolean) => void;
}

const BOARD_AREA = { left: 20, right: 700, top: 170, bottom: 850 };

/**
 * 游戏页（任务书 7.2）：网格关卡和曲线关卡的棋盘。
 *
 * 视图只消费快照：它从不重新计算几何、从不判定
 * 收集，也从不更改玩法状态。动画位于
 * `CarView`/`EventDirector`/`Anims` 中，且纯粹是视觉效果（5.7 A-1/A-2）。
 */
export class GameView {
    readonly root: Node;
    private readonly layoutLayer: Node;
    private readonly roadLayer: Node;
    private readonly fruitLayer: Node;
    private readonly parkingLayer: Node;
    private readonly hud: Hud;
    private readonly itemBar: ItemBar;
    private readonly toast: Label;
    private layout: BoardLayout | null = null;
    private layoutKey = '';
    private roadRenderer: RoadRenderer | null = null;
    private fruitRenderer: FruitRenderer | null = null;
    private carRenderer: CarRenderer | null = null;
    private director: EventDirector | null = null;
    private level: LevelSummary | null = null;
    private lastSnapshot: Snapshot | null = null;
    private nodeCounters = { fruit: 0, road: 0, lanes: 0, parked: 0 };
    private mismatchCount = 0;
    private snapshotCount = 0;
    private input: InputController | null = null;
    private world: 'grid' | 'curves' = 'grid';
    private renderStats = { collectAnims: 0, refillAnims: 0, eventsConsumed: 0 };

    constructor(parent: Node, bridge: GameBridge, callbacks: GameViewCallbacks) {
        this.root = uiNode(parent, 'GameScreen', 720, 1280);
        const background = addBlock(this.root, 'Background', 720, 1280, new Color(18, 22, 30, 255));
        background.setPosition(0, 0, 0);
        background.setSiblingIndex(0);

        this.layoutLayer = uiNode(this.root, 'BoardLayer', 720, 1280);
        this.roadLayer = uiNode(this.layoutLayer, 'RoadLayer', 720, 1280);
        this.fruitLayer = uiNode(this.layoutLayer, 'FruitLayer', 720, 1280);
        this.parkingLayer = uiNode(this.layoutLayer, 'ParkingLayer', 720, 1280);
        const uiLayer = uiNode(this.root, 'UILayer', 720, 1280);

        this.hud = new Hud(uiLayer, {
            onBack: callbacks.onBack,
            onRestart: callbacks.onRestart,
            onHome: callbacks.onHome,
        });
        this.itemBar = new ItemBar(uiLayer, (itemId) => this.activateItem(itemId),
            (message, accepted) => this.showFeedback(message, accepted));
        this.toast = addLabelSafe(uiLayer);
        this.input = new InputController(bridge, (message, accepted) => {
            this.showFeedback(message, accepted);
            callbacks.onFeedback(message, accepted);
        });
        // A-3：道路车辆每个渲染帧追赶一次快照。director
        // 更新事件不需要组件，因此即使脚本注册失败，
        // 也不会破坏场景。
        director.on(Director.EVENT_BEFORE_UPDATE, this.onFrame, this);
    }

    private onFrame(): void {
        const dt = game.deltaTime;
        if (dt > 0 && dt < 0.5) {
            this.update(dt);
        }
    }

    setLevel(level: LevelSummary | null): void {
        this.level = level;
        this.layoutKey = '';
        this.lastSnapshot = null;
        if (level && this.layoutLayer) {
            for (const child of [...this.roadLayer.children, ...this.fruitLayer.children]) {
                child.destroy();
            }
        }
    }

    setVisible(visible: boolean): void {
        this.root.active = visible;
        this.hud.setVisible(visible);
        if (!visible) {
            this.lastSnapshot = null;
        }
    }

    /** A-3：道路车辆的逐帧追赶。 */
    update(dt: number): void {
        if (this.carRenderer) {
            const speed = this.lastSnapshot?.road.speed ?? 2.0;
            this.carRenderer.update(dt, speed);
        }
    }

    private showFeedback(message: string, accepted: boolean): void {
        if (accepted) {
            this.toast.string = '';
            return;
        }
        this.toast.string = message;
        this.toast.node.active = true;
        setTimeout(() => {
            if (this.toast) {
                this.toast.string = '';
                this.toast.node.active = false;
            }
        }, 1200);
    }

    /** 预留技能：点击道具槽并将 `use_item` 转发给 host。 */
    private activateItem(itemId: string): void {
        const bridge = (globalThis as unknown as { __fcBridge?: GameBridge }).__fcBridge;
        if (!bridge) {
            return;
        }
        bridge.useItem(itemId).then((receipt) => {
            const text = receipt.accepted ? '道具已使用' : `道具被拒绝：${receipt.reason}`;
            this.showFeedback(text, false);
            console.log('[FC_ITEM]' + JSON.stringify({ itemId, accepted: receipt.accepted,
                reason: receipt.reason }));
        }).catch((error: unknown) => {
            this.showFeedback(error instanceof Error ? error.message : '网络错误', false);
        });
    }

    private ensureLayout(snapshot: Snapshot): void {
        const curve = (snapshot.world ?? 'grid') === 'curves';
        this.world = curve ? 'curves' : 'grid';
        const polyline = snapshot.road.polyline ?? [];
        const fruits = snapshot.fruits ?? [];
        const track = snapshot.road.track ?? snapshot.road.cells;
        const key = [
            snapshot.levelId, snapshot.levelEpoch, this.world,
            curve ? polyline.length : track.length,
            `${snapshot.grid.rows}x${snapshot.grid.cols}`,
        ].join(':');
        if (key === this.layoutKey && this.layout && this.fruitRenderer && this.carRenderer) {
            return;
        }
        this.layoutKey = key;
        this.layout = curve
            ? BoardLayout.forCurve(polyline, fruits, BOARD_AREA)
            : BoardLayout.forGrid(snapshot.grid.rows, snapshot.grid.cols, track, BOARD_AREA);
        this.roadLayer.removeAllChildren();
        this.fruitLayer.removeAllChildren();
        this.parkingLayer.removeAllChildren();

        this.roadRenderer = new RoadRenderer(this.roadLayer);
        this.roadRenderer.sync(snapshot, this.layout, this.level);
        this.fruitRenderer = new FruitRenderer(this.fruitLayer, this.layout);
        this.carRenderer = new CarRenderer(this.roadLayer, this.parkingLayer, this.layout,
            (kind, index) => this.handleClick(kind, index));
        this.director = new EventDirector(this.fruitRenderer, this.carRenderer);
    }

    private handleClick(kind: 'lane' | 'slot', index: number): void {
        if (!this.input) {
            return;
        }
        this.input.click(kind, index).catch((error: unknown) => {
            this.showFeedback(error instanceof Error ? error.message : '网络错误', false);
        });
    }

    applySnapshot(snapshot: Snapshot): void {
        this.lastSnapshot = snapshot;
        this.ensureLayout(snapshot);
        if (!this.layout || !this.fruitRenderer || !this.carRenderer || !this.director) {
            return;
        }
        // 1. 一次性动画在状态重新同步之前决定，这样它们要
        //    动画的 node 仍然存在（见 EventDirector）。
        this.director.consume(snapshot, this.layout);
        // 2. 状态重新同步（位置、尺寸、颜色、node 创建/移除）。
        this.roadRenderer?.sync(snapshot, this.layout, this.level);
        this.fruitRenderer.sync(snapshot, this.layout);
        this.carRenderer.sync(snapshot, this.layout);
        this.director.afterSync();
        // 3. HUD + 道具栏。
        this.hud.update(snapshot);
        this.itemBar.update(snapshot);
        const counts = this.carRenderer.counts();
        const stats = this.director.statistics;
        this.renderStats = {
            collectAnims: stats.collectAnimations,
            refillAnims: stats.refillAnimations,
            eventsConsumed: stats.consumed,
        };
        this.nodeCounters = {
            fruit: this.fruitRenderer.nodeCount(),
            road: counts.road,
            lanes: counts.lanes,
            parked: counts.parked,
        };
        this.reportConsistency(snapshot);
    }

    /**
     * 每秒输出一行可供机器检查的日志（并在任何不匹配时输出），
     * 使视觉证据能够证明渲染的 node 数量与快照一致。
     */
    private reportConsistency(snapshot: Snapshot): void {
        const curve = this.world === 'curves';
        let fruitCells = 0;
        if (curve) {
            fruitCells = (snapshot.fruits ?? []).length;
        } else {
            for (const row of snapshot.grid.cells) {
                for (const cell of row) {
                    if (cell) {
                        fruitCells += 1;
                    }
                }
            }
        }
        const laneCars = snapshot.lanes.reduce((sum, lane) => sum + lane.queue.length, 0);
        const occupied = snapshot.parking.slots.reduce((sum, slot) => sum + (slot ? 1 : 0), 0);
        const roadCells = curve ? 1 : (snapshot.road.track ?? []).length;
        const roadNodes = this.roadRenderer ? this.roadRenderer.count() : 0;
        const mismatches =
            (this.nodeCounters.fruit === fruitCells ? 0 : 1) +
            (this.nodeCounters.road === snapshot.vehicles.length ? 0 : 1) +
            (this.nodeCounters.lanes === laneCars ? 0 : 1) +
            (this.nodeCounters.parked === occupied ? 0 : 1) +
            (roadNodes === roadCells ? 0 : 1);
        this.mismatchCount += mismatches;
        this.snapshotCount += 1;
        if (this.snapshotCount % 15 !== 0 && mismatches === 0) {
            return;
        }
        const payload = {
            tick: snapshot.tick,
            seed: snapshot.seed,
            levelId: snapshot.levelId,
            levelEpoch: snapshot.levelEpoch,
            world: this.world,
            check: 'node_counts',
            fruitNodes: this.nodeCounters.fruit,
            fruitCells,
            roadNodes: this.nodeCounters.road,
            roadCars: snapshot.vehicles.length,
            laneNodes: this.nodeCounters.lanes,
            laneCars,
            parkedNodes: this.nodeCounters.parked,
            occupiedSlots: occupied,
            trackCells: roadCells,
            trackNodes: roadNodes,
            collectAnims: this.renderStats.collectAnims,
            refillAnims: this.renderStats.refillAnims,
            eventsConsumed: this.renderStats.eventsConsumed,
            mismatches,
            layout: this.layout ? this.layout.debugInfo() : null,
        };
        console.log('[FC_CONSISTENCY]' + JSON.stringify(payload));
        const bridge = (globalThis as unknown as { __fcBridge?: GameBridge }).__fcBridge;
        if (bridge) {
            bridge.report(payload).catch(() => undefined);
        }
    }

    /** 供 M18/M19/M24 一致性检查使用的只读探针。 */
    consistency(): Record<string, unknown> {
        const snapshot = this.lastSnapshot;
        if (!snapshot) {
            return { ok: false, reason: 'no snapshot' };
        }
        const curve = this.world === 'curves';
        let fruitCells = 0;
        if (curve) {
            fruitCells = (snapshot.fruits ?? []).length;
        } else {
            for (const row of snapshot.grid.cells) {
                for (const cell of row) {
                    if (cell) {
                        fruitCells += 1;
                    }
                }
            }
        }
        return {
            ok: true,
            tick: snapshot.tick,
            seed: snapshot.seed,
            levelId: snapshot.levelId,
            levelEpoch: snapshot.levelEpoch,
            world: this.world,
            status: snapshot.status,
            nodes: this.nodeCounters,
            snapshot: {
                fruitCells,
                roadCars: snapshot.vehicles.length,
                laneCars: snapshot.lanes.reduce((sum, lane) => sum + lane.queue.length, 0),
                trackCells: curve ? 1 : (snapshot.road.track ?? []).length,
                polylinePoints: (snapshot.road.polyline ?? []).length,
                items: (snapshot.items ?? []).length,
            },
            animations: this.renderStats,
            layoutKey: this.layoutKey,
            mismatches: this.mismatchCount,
        };
    }

    get hudRef(): Hud {
        return this.hud;
    }

    get itemBarRef(): ItemBar {
        return this.itemBar;
    }
}

function addLabelSafe(parent: Node): Label {
    const holder = uiNode(parent, 'Txt_toast', 560, 36);
    const component = holder.addComponent(Label);
    component.string = '';
    component.fontSize = 22;
    component.lineHeight = 26;
    component.color = new Color(255, 210, 120, 255);
    holder.setPosition(new Vec3(0, 470, 0));
    holder.active = false;
    return component;
}
