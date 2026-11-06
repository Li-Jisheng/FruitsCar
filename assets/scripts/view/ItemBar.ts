import { Color, EventTouch, Label, Node, Vec3 } from 'cc';
import { Snapshot, SnapshotItem } from '../bridge/GameBridge';
import { addBlock, addLabel, setBlockColor, uiNode } from './SpriteFactory';

const IDLE = new Color(86, 96, 120, 255);
const HOT = new Color(130, 150, 190, 255);
const DISABLED = new Color(56, 62, 78, 255);

/**
 * 与快照 `items[]` 关联的道具栏（任务书 7 T-25 / 8 T-33）。
 *
 * 每个槽位显示道具 id 和剩余次数。点击（或将图标拖回
 * 原位）会发送 `use_item`；当前阶段 host 会返回稳定的
 * 拒绝原因，并把消息展示给玩家。
 */
export class ItemBar {
    private readonly slots: Node[] = [];
    private readonly nameLabels: Label[] = [];
    private readonly chargeLabels: Label[] = [];
    private readonly homes: Vec3[] = [];
    private items: SnapshotItem[] = [];
    private dragged: Node | null = null;
    private lastRejection = '';

    constructor(layer: Node,
                private readonly onUse: (itemId: string) => void,
                private readonly onFeedback: (message: string, accepted: boolean) => void) {
        for (let i = 0; i < 3; i += 1) {
            const node = addBlock(layer, `Item_${i}`, 92, 92, IDLE);
            const home = new Vec3(-160 + i * 160, -560, 0);
            node.setPosition(home);
            this.homes.push(home);
            this.slots.push(node);
            const name = addLabel(node, 'Txt_item', 86, 20, '', 15, new Color(240, 246, 255, 255));
            name.node.setPosition(new Vec3(0, 12, 0));
            this.nameLabels.push(name);
            const charge = addLabel(node, 'Txt_charges', 86, 18, '', 13, new Color(255, 214, 130, 255));
            charge.node.setPosition(new Vec3(0, -16, 0));
            this.chargeLabels.push(charge);

            const index = i;
            node.on(Node.EventType.TOUCH_START, (_event: EventTouch) => {
                this.dragged = node;
                if (this.items[index]) {
                    setBlockColor(node, HOT);
                }
            });
            node.on(Node.EventType.TOUCH_MOVE, (event: EventTouch) => {
                if (this.dragged !== node) {
                    return;
                }
                const delta = event.getUIDelta();
                node.setPosition(node.position.x + delta.x, node.position.y + delta.y, 0);
            });
            node.on(Node.EventType.TOUCH_END, (_event: EventTouch) => {
                const moved = Vec3.distance(node.position, this.homes[index]);
                this.reset(index);
                if (moved < 18) {
                    this.activate(index);
                }
            });
            node.on(Node.EventType.TOUCH_CANCEL, (_event: EventTouch) => {
                this.reset(index);
            });
        }
    }

    /** 读取 `snapshot.items`；host 是剩余次数的唯一来源。 */
    update(snapshot: Snapshot): void {
        this.items = snapshot.items ?? [];
        for (let i = 0; i < this.slots.length; i += 1) {
            const item = this.items[i];
            if (!item) {
                this.slots[i].active = false;
                continue;
            }
            this.slots[i].active = true;
            this.nameLabels[i].string = shortName(item);
            this.chargeLabels[i].string = `x${Math.max(0, item.charges)}`;
            setBlockColor(this.slots[i], item.charges > 0 ? IDLE : DISABLED);
        }
    }

    private activate(index: number): void {
        const item = this.items[index];
        if (!item) {
            return;
        }
        if (item.charges <= 0) {
            this.onFeedback(`道具 ${item.id} 已用尽`, false);
            return;
        }
        this.lastRejection = '';
        this.onUse(item.id);
    }

    private reset(index: number): void {
        const node = this.slots[index];
        node.setPosition(this.homes[index]);
        const item = this.items[index];
        setBlockColor(node, item && item.charges > 0 ? IDLE : DISABLED);
        this.dragged = null;
    }

    /** 为将来接入效果而保留的钩子（兼容第一阶段）。 */
    onItemActivatedHook(id: string): void {
        this.onUse(id);
    }

    nodes(): Node[] {
        return this.slots;
    }

    itemCount(): number {
        return this.items.length;
    }

    get lastReason(): string {
        return this.lastRejection;
    }
}

function shortName(item: SnapshotItem): string {
    switch (item.kind) {
        case 'recolor_car':
            return '换色';
        case 'collect_area':
            return '收一片';
        default:
            return item.id;
    }
}

/** 用于拖动起始向量的小型辅助函数（为将来接入效果而保留）。 */
export function emptyNode(parent: Node, name: string): Node {
    return uiNode(parent, name, 1, 1);
}
