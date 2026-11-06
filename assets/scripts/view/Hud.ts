import { Color, EventTouch, Label, Node } from 'cc';
import { Snapshot } from '../bridge/GameBridge';
import { ResultPanel } from './ResultPanel';
import { addBlock, addLabel, uiNode } from './SpriteFactory';
import { block, label, node, rounded, triangle } from './UiKit';

export interface HudCallbacks {
    onBack: () => void;
    onRestart: () => void;
    onHome: () => void;
}

/**
 * 游戏内 HUD（任务书 7.7）：返回按钮、关卡名称、剩余数量、
 * 下一车道提示（仅宽关卡）、需要选车横幅以及结算
 * 面板。所有数值都来自快照。
 */
export class Hud {
    readonly root: Node;
    readonly txtRemaining: Label;
    readonly txtLevelName: Label;
    readonly txtNextLane: Label;
    readonly nextLaneArrow: Node;
    readonly txtInfo: Label;
    readonly txtEvidence: Label;
    readonly banner: Node;
    readonly bannerText: Label;
    readonly result: ResultPanel;

    constructor(layer: Node, callbacks: HudCallbacks) {
        this.root = node(layer, 'Hud', 720, 1280);

        const back = rounded(this.root, 'Btn_back', 72, 66, new Color(30, 36, 48, 255), 14);
        back.setPosition(-312, 596, 0);
        const backText = label(back, 'Txt_back', 60, 30, '返回', 17, new Color(226, 234, 248, 255));
        backText.node.setPosition(0, 0, 0);
        back.on(Node.EventType.TOUCH_END, (_event: EventTouch) => callbacks.onBack());

        this.txtLevelName = label(this.root, 'Txt_level', 420, 40, '', 30, new Color(232, 238, 250, 255), true);
        this.txtLevelName.node.setPosition(0, 600, 0);

        this.txtRemaining = label(this.root, 'Txt_remaining', 200, 26, '剩余 0', 19, new Color(242, 193, 78, 255));
        this.txtRemaining.node.setPosition(250, 600, 0);

        this.txtNextLane = label(this.root, 'Txt_next_lane', 200, 24, '', 15, new Color(138, 147, 166, 255));
        this.txtNextLane.node.setPosition(250, 570, 0);
        this.nextLaneArrow = triangle(this.root, 'NextLaneArrow', 10, new Color(138, 147, 166, 255), 0);
        this.nextLaneArrow.setPosition(160, 570, 0);
        this.nextLaneArrow.active = false;

        // 曲线关卡（任务书 7 T-26）：层数和预留道具次数。
        this.txtInfo = label(this.root, 'Txt_curve_info', 320, 24, '', 15, new Color(120, 200, 170, 255));
        this.txtInfo.node.setPosition(-170, 570, 0);
        this.txtInfo.node.active = false;

        // 可供机器读取的证据条：seed + tick 在每张截图中都保持可见
        // （第一阶段约定，对回放检查仍然有用）。
        this.txtEvidence = label(this.root, 'Txt_evidence', 640, 20, '', 12, new Color(108, 118, 140, 255));
        this.txtEvidence.node.setPosition(0, -626, 0);

        this.banner = addBlock(this.root, 'Banner_require_select', 620, 44, new Color(196, 64, 64, 235));
        this.banner.setPosition(0, 540, 0);
        this.bannerText = addLabel(this.banner, 'Txt_banner', 600, 30, '请从停车位选择车辆', 20,
            new Color(255, 255, 255, 255));
        this.bannerText.node.setPosition(0, 0, 0);
        this.banner.active = false;

        this.result = new ResultPanel(layer, { onRestart: callbacks.onRestart, onHome: callbacks.onHome });
    }

    update(snapshot: Snapshot): void {
        this.txtLevelName.string = snapshot.hud.levelName || snapshot.levelId;
        this.txtRemaining.string = `剩余 ${snapshot.hud.fruitsRemaining}`;
        this.txtEvidence.string = `seed ${snapshot.seed} · tick ${snapshot.tick} · epoch ${snapshot.levelEpoch} · ${snapshot.world ?? 'grid'}`;
        this.banner.active = snapshot.hud.requireSelect;

        const curve = (snapshot.world ?? 'grid') === 'curves';
        this.txtInfo.node.active = curve;
        if (curve) {
            this.txtInfo.string = `曲线 · ${snapshot.hud.layerCount ?? 0} 层 · 道具 ${snapshot.hud.itemCharges ?? 0}`;
        }

        const multiLane = snapshot.hud.laneCount > 1;
        this.txtNextLane.node.active = multiLane;
        this.nextLaneArrow.active = multiLane;
        if (multiLane) {
            const lane = snapshot.hud.nextLane;
            const right = lane === 1;
            this.txtNextLane.string = `下一辆：${right ? '右道' : '左道'}`;
            // 让箭头指向下一辆车将要收集的那一侧。
            const side = snapshot.road.lanes[lane] ? snapshot.road.lanes[lane].collectSide : 'left';
            this.nextLaneArrow.angle = side === 'right' ? 90 : -90;
            this.nextLaneArrow.setPosition(side === 'right' ? 350 : 150, 570, 0);
        }
        this.result.update(snapshot);
    }

    setVisible(visible: boolean): void {
        this.root.active = visible;
        if (!visible) {
            this.result.root.active = false;
        }
    }
}

export function createHudLayer(parent: Node): Node {
    return uiNode(parent, 'HudLayer', 720, 1280);
}
