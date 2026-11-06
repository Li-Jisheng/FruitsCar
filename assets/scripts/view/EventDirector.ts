import { Vec3 } from 'cc';
import { Snapshot, SnapshotEvent } from '../bridge/GameBridge';
import { BoardLayout } from './BoardLayout';
import { CarRenderer } from './CarRenderer';
import { FruitRenderer } from './FruitRenderer';

export interface DirectorStats {
    consumed: number;
    collected: number;
    refilled: number;
    entered: number;
    parked: number;
    exited: number;
    results: number;
    items: number;
    collectAnimations: number;
    refillAnimations: number;
}

interface PendingRefill {
    key: string;
    from: Vec3;
}

/**
 * 消费 `snapshot.events`，并将每个一次性玩法事件转换为
 * 展示动画（任务书 5.7 A-6）。
 *
 * 顺序很重要：`consume()` 在渲染器重新同步之前运行，因此
 * 被收集的水果 node 仍存在于旧位置；补充起始位置
 * 会在此刻被捕获，然后在 node 移动至新槽位后
 * 于 `afterSync()` 中回放。
 *
 * 事件按 `seq` 去重，因此丢失或重复的快照绝不会
 * 将同一动画播放两次（任务书 7 T-23）。
 */
export class EventDirector {
    private lastSeq = 0;
    private pendingRefill: PendingRefill[] = [];
    private readonly stats: DirectorStats = {
        consumed: 0, collected: 0, refilled: 0, entered: 0, parked: 0, exited: 0, results: 0,
        items: 0, collectAnimations: 0, refillAnimations: 0,
    };

    constructor(private readonly fruits: FruitRenderer, private readonly cars: CarRenderer) {}

    reset(): void {
        this.lastSeq = 0;
        this.pendingRefill = [];
    }

    consume(snapshot: Snapshot, layout: BoardLayout): void {
        const curve = (snapshot.world ?? 'grid') === 'curves';
        for (const event of snapshot.events) {
            if (event.seq <= this.lastSeq) {
                continue;
            }
            this.lastSeq = event.seq;
            this.stats.consumed += 1;
            this.dispatch(snapshot, event, layout, curve);
        }
    }

    private dispatch(snapshot: Snapshot, event: SnapshotEvent, layout: BoardLayout,
                     curve: boolean): void {
        switch (event.type) {
            case 'FRUIT_COLLECTED': {
                this.stats.collected += 1;
                const key = this.fruitKey(event, curve);
                const target = event.car ? this.cars.viewPosition(event.car) : null;
                if (target && this.fruits.playCollect(key, target)) {
                    this.stats.collectAnimations += 1;
                    if (event.car) {
                        this.cars.pulseCar(event.car);
                    }
                }
                break;
            }
            case 'FRUIT_REFILLED': {
                this.stats.refilled += 1;
                const key = this.fruitKey(event, curve);
                const node = this.fruits.nodeOf(key);
                const fallback = this.fallbackRefillStart(event, layout, curve);
                this.pendingRefill.push({ key, from: node ? node.position.clone() : fallback });
                break;
            }
            case 'VEHICLE_ENTERED':
                this.stats.entered += 1;
                break;
            case 'VEHICLE_PARKED':
                this.stats.parked += 1;
                break;
            case 'VEHICLE_EXITED':
                this.stats.exited += 1;
                break;
            case 'RESULT':
                this.stats.results += 1;
                break;
            case 'ITEM_REJECTED':
            case 'ITEM_ACTIVATED':
            case 'EFFECT_APPLIED':
            case 'EFFECT_EXPIRED':
                this.stats.items += 1;
                break;
            default:
                break;
        }
    }

    private fruitKey(event: SnapshotEvent, curve: boolean): string {
        if (curve && typeof event.id === 'number') {
            return FruitRenderer.keyForFruit(event.id);
        }
        if (event.at) {
            return FruitRenderer.keyForCell(event.at);
        }
        return `unknown:${event.seq}`;
    }

    private fallbackRefillStart(event: SnapshotEvent, layout: BoardLayout, curve: boolean): Vec3 {
        if (curve && typeof event.fromX === 'number' && typeof event.fromY === 'number') {
            return layout.positionOfWorld(event.fromX, event.fromY);
        }
        if (event.from) {
            return layout.positionOf(event.from[0], event.from[1]);
        }
        return new Vec3();
    }

    /** 在渲染器移动到新状态后启动滑动动画。 */
    afterSync(): void {
        for (const pending of this.pendingRefill) {
            if (this.fruits.playRefill(pending.key, pending.from)) {
                this.stats.refillAnimations += 1;
            }
        }
        this.pendingRefill = [];
    }

    get statistics(): DirectorStats {
        return this.stats;
    }
}
