import { Color, EventTouch, Label, Node, Vec3 } from 'cc';
import { Snapshot, SnapshotVehicle } from '../bridge/GameBridge';
import { BoardLayout } from './BoardLayout';
import { CarView } from './CarView';
import { addBlock, addLabel, paletteColor, setBlockColor, uiNode } from './SpriteFactory';

export type CarClick = (kind: 'lane' | 'slot', index: number) => void;

const LANE_X = [-270, -180, -90];
const LANE_TOP = -230;
const SLOT_Y = -470;

interface LaneEntry {
    node: Node;
    lane: number;
}

interface ParkedEntry {
    node: Node;
    slot: { value: number };
}

/**
 * 道路车辆、车道队列和停车位（任务书 7 T-22）。
 *
 * 道路车辆是由帧更新驱动的 `CarView` 实例（A-3），而
 * 队列/停车车辆是 host 状态的静态快照（它们不会移动）。
 */
export class CarRenderer {
    private readonly carViews = new Map<string, CarView>();
    private readonly laneNodes = new Map<string, LaneEntry>();
    private readonly parkedNodes = new Map<string, ParkedEntry>();
    private readonly contactLines = new Map<string, Node>();

    constructor(private readonly roadLayer: Node,
                private readonly parkingLayer: Node,
                private readonly layout: BoardLayout,
                private readonly onClick: CarClick) {
        this.buildStaticSlots();
        this.buildLaneFrames();
    }

    private buildLaneFrames(): void {
        for (let lane = 0; lane < LANE_X.length; lane += 1) {
            const frame = addBlock(this.parkingLayer, `LaneFrame_${lane}`, 86, 250,
                new Color(38, 44, 58, 255));
            frame.setPosition(new Vec3(LANE_X[lane], LANE_TOP - 100, 0));
        }
    }

    private buildStaticSlots(): void {
        for (let i = 0; i < 5; i += 1) {
            const slot = addBlock(this.parkingLayer, `Slot_${i}`, 110, 80, new Color(40, 48, 64, 255));
            slot.setPosition(new Vec3(-240 + i * 120, SLOT_Y, 0));
            const text = addLabel(slot, 'Txt_slot', 100, 18, `${i}`, 14, new Color(150, 160, 180, 255));
            text.node.setPosition(new Vec3(0, -30, 0));
        }
    }

    sync(snapshot: Snapshot, layout: BoardLayout): void {
        this.syncRoad(snapshot);
        this.syncLanes(snapshot);
        this.syncParking(snapshot);
    }

    /** 每帧对每辆道路车辆进行插值（任务书 5.7 A-3）。 */
    update(dt: number, speed: number): void {
        const maxStep = 2 * Math.max(0.1, speed) * dt * this.layout.cellSize;
        for (const view of this.carViews.values()) {
            view.update(dt, maxStep, 540);
        }
    }

    private positionOf(snapshot: Snapshot, car: SnapshotVehicle): Vec3 {
        const [row, col] = car.pos ?? car.cell;
        return this.layout.positionOf(row, col);
    }

    private syncRoad(snapshot: Snapshot): void {
        const seen = new Set<string>();
        const cellSize = this.layout.cellSize;
        // 1.05 x 0.62 个网格单元格（任务书 4.7）；车身沿 +x 方向绘制，
        // 并按 host 给出的朝向旋转。
        const width = Math.max(18, cellSize * 1.05);
        const height = Math.max(14, cellSize * 0.62);
        for (const car of snapshot.vehicles) {
            seen.add(car.id);
            const position = this.positionOf(snapshot, car);
            let view = this.carViews.get(car.id);
            if (!view) {
                view = new CarView(this.roadLayer, car.id, width, height, car.color, car.label);
                this.carViews.set(car.id, view);
                view.setTarget(position, car.heading ?? 0, true);
                const rad = (car.heading ?? 0) * Math.PI / 180;
                const behind = new Vec3(position.x - Math.cos(rad) * cellSize * 1.4,
                    position.y + Math.sin(rad) * cellSize * 1.4, 0);
                view.playEnter(behind);
            } else {
                view.setSize(width, height);
                view.setTarget(position, car.heading ?? 0);
            }
            view.setLabel(car.label);
            view.setColor(car.color, car.state === 'WAITING');
            this.syncContactLine(snapshot, car, position, cellSize);
        }
        for (const [id, view] of this.carViews) {
            if (!seen.has(id)) {
                view.playExit(() => undefined);
                this.carViews.delete(id);
            }
        }
        for (const [id, line] of this.contactLines) {
            if (!seen.has(id)) {
                line.destroy();
                this.contactLines.delete(id);
            }
        }
    }

    /**
     * “接触反馈”：在车辆可触及的一侧绘制 2 px 的彩色线，使
     * “仅有一侧”从隐含信息变为可见。
     */
    private syncContactLine(snapshot: Snapshot, car: SnapshotVehicle, point: Vec3, cellSize: number): void {
        let line = this.contactLines.get(car.id);
        const lane = snapshot.road.lanes[car.lane];
        const collectSide = lane ? lane.collectSide : snapshot.road.collectSide;
        if (collectSide === 'none') {
            if (line) {
                line.active = false;
            }
            return;
        }
        if (!line) {
            line = addBlock(this.roadLayer, `Contact_${car.id}`, 2.5, cellSize * 0.9,
                new Color(255, 255, 255, 90));
            this.contactLines.set(car.id, line);
        }
        line.active = true;
        setBlockColor(line, new Color(255, 255, 255, 110));
        const heading = car.heading ?? 0;
        const vertical = heading === 90 || heading === 270;
        const transform = line.getComponent('cc.UITransform') as unknown as
            { setContentSize: (w: number, h: number) => void } | null;
        if (transform) {
            transform.setContentSize(vertical ? cellSize * 0.9 : 2.5, vertical ? 2.5 : cellSize * 0.9);
        }
        const right = collectSide === 'right';
        let offsetX = 0;
        let offsetY = 0;
        if (vertical) {
            offsetX = right ? -cellSize * 0.5 : cellSize * 0.5;
        } else {
            offsetY = right ? cellSize * 0.5 : -cellSize * 0.5;
        }
        line.setPosition(new Vec3(point.x + offsetX, point.y + offsetY, 0));
    }

    private syncLanes(snapshot: Snapshot): void {
        const seen = new Set<string>();
        for (const lane of snapshot.lanes) {
            const baseX = LANE_X[lane.index] !== undefined ? LANE_X[lane.index] : LANE_X[0];
            for (let q = 0; q < lane.queue.length; q += 1) {
                const id = lane.queue[q];
                seen.add(id);
                let entry = this.laneNodes.get(id);
                if (!entry) {
                    const created = this.buildCar(this.parkingLayer, `LaneCar_${id}`, 78, 46,
                        lane.color, '0/4');
                    const laneIndex = lane.index;
                    created.on(Node.EventType.TOUCH_END, (_event: EventTouch) => {
                        this.onClick('lane', laneIndex);
                    });
                    entry = { node: created, lane: lane.index };
                    this.laneNodes.set(id, entry);
                }
                entry.node.setPosition(new Vec3(baseX, LANE_TOP - q * 54, 0));
                const info = snapshot.vehicleInfo[id];
                const text = entry.node.getChildByName('Txt_load')?.getComponent(Label);
                if (text && info) {
                    text.string = info.label;
                }
            }
        }
        for (const [id, entry] of this.laneNodes) {
            if (!seen.has(id)) {
                entry.node.destroy();
                this.laneNodes.delete(id);
            }
        }
    }

    private syncParking(snapshot: Snapshot): void {
        const seen = new Set<string>();
        for (let i = 0; i < snapshot.parking.slots.length; i += 1) {
            const slot = snapshot.parking.slots[i];
            if (!slot) {
                continue;
            }
            seen.add(slot.vehicle);
            let entry = this.parkedNodes.get(slot.vehicle);
            if (!entry) {
                const created = this.buildCar(this.parkingLayer, `ParkedCar_${slot.vehicle}`, 100, 56,
                    slot.color, slot.label);
                const holder = { value: i };
                created.on(Node.EventType.TOUCH_END, (_event: EventTouch) => {
                    this.onClick('slot', holder.value);
                });
                entry = { node: created, slot: holder };
                this.parkedNodes.set(slot.vehicle, entry);
            }
            entry.slot.value = i;
            entry.node.setPosition(new Vec3(-240 + i * 120, SLOT_Y, 0));
            const text = entry.node.getChildByName('Txt_load')?.getComponent(Label);
            if (text) {
                text.string = slot.label;
            }
        }
        for (const [id, entry] of this.parkedNodes) {
            if (!seen.has(id)) {
                entry.node.destroy();
                this.parkedNodes.delete(id);
            }
        }
    }

    private buildCar(parent: Node, name: string, width: number, height: number, colorId: string,
                     text: string): Node {
        const container = uiNode(parent, name, width, height);
        const body = addBlock(container, 'Body', width, height, paletteColor(colorId));
        body.setPosition(0, 0, 0);
        const textLabel = addLabel(container, 'Txt_load', width, 16, text, 13,
            new Color(20, 22, 28, 255));
        textLabel.node.setPosition(new Vec3(0, 0, 0));
        return container;
    }

    /** 道路车辆的 canvas 位置（供收集动画使用）。 */
    viewPosition(id: string): Vec3 | null {
        return this.carViews.get(id)?.position() ?? null;
    }

    pulseCar(id: string): void {
        this.carViews.get(id)?.pulse();
    }

    counts(): { road: number; lanes: number; parked: number } {
        return { road: this.carViews.size, lanes: this.laneNodes.size, parked: this.parkedNodes.size };
    }
}
