import { Color, EventTouch, Label, Node } from 'cc';
import { Snapshot } from '../bridge/GameBridge';
import { DESIGN_HEIGHT, DESIGN_WIDTH, at, block, label, node, rounded } from './UiKit';

export interface ResultCallbacks {
    onRestart: () => void;
    onHome: () => void;
}

/**
 * 结算面板（任务书 7.7）：覆盖在半透明遮罩上的 520x300 面板。标题
 * 仅由快照状态驱动——客户端从不推断结果。
 */
export class ResultPanel {
    readonly root: Node;
    private readonly title: Label;
    private readonly detail: Label;
    private readonly progress: Label;

    constructor(layer: Node, callbacks: ResultCallbacks) {
        this.root = node(layer, 'ResultPanel', DESIGN_WIDTH, DESIGN_HEIGHT);
        this.root.setPosition(0, 0, 0);
        block(this.root, 'Mask', DESIGN_WIDTH, DESIGN_HEIGHT, new Color(6, 8, 12, 185));

        const panel = rounded(this.root, 'PanelBg', 520, 300, new Color(24, 29, 40, 250), 24,
            new Color(58, 70, 96, 255), 2);
        panel.setPosition(0, 0, 0);

        this.title = label(panel, 'Txt_title', 460, 64, '', 46, new Color(255, 255, 255, 255), true);
        this.title.node.setPosition(0, 88, 0);
        this.detail = label(panel, 'Txt_detail', 460, 32, '', 20, new Color(200, 210, 232, 255));
        this.detail.node.setPosition(0, 34, 0);
        this.progress = label(panel, 'Txt_progress', 460, 28, '', 17, new Color(150, 160, 186, 255));
        this.progress.node.setPosition(0, 2, 0);

        const again = rounded(panel, 'Btn_again', 200, 64, new Color(64, 132, 232, 255), 16);
        again.setPosition(-116, -84, 0);
        const againText = label(again, 'Txt_again', 180, 40, '再来一次', 24, new Color(255, 255, 255, 255), true);
        againText.node.setPosition(0, 0, 0);
        again.on(Node.EventType.TOUCH_END, (_event: EventTouch) => callbacks.onRestart());

        const home = rounded(panel, 'Btn_home', 200, 64, new Color(46, 56, 76, 255), 16);
        home.setPosition(116, -84, 0);
        const homeText = label(home, 'Txt_home', 180, 40, '返回主页', 24, new Color(226, 234, 248, 255), true);
        homeText.node.setPosition(0, 0, 0);
        home.on(Node.EventType.TOUCH_END, (_event: EventTouch) => callbacks.onHome());

        this.root.active = false;
    }

    get visible(): boolean {
        return this.root.active;
    }

    update(snapshot: Snapshot): void {
        const result = snapshot.hud.result;
        this.root.active = result !== null;
        if (result === null) {
            return;
        }
        const seconds = ((snapshot.hud.resultTick ?? snapshot.tick) / 60).toFixed(1);
        if (result === 'WIN') {
            this.title.string = '通关';
            this.title.color = new Color(120, 220, 140, 255);
            this.detail.string = `用时 ${seconds}s · 收集 ${snapshot.hud.fruitsCollected}/${snapshot.hud.fruitsTotal}`;
        } else if (result === 'FAIL_TRAFFIC_JAM') {
            this.title.string = '堵车';
            this.title.color = new Color(240, 110, 110, 255);
            this.detail.string = `入口堵塞 · 车位已满 · 末端等待`;
        } else {
            this.title.string = result;
            this.title.color = new Color(230, 230, 230, 255);
            this.detail.string = `tick ${snapshot.hud.resultTick}`;
        }
        // 宽关卡的区域进度（任务书 6.11 第 7 项）。
        if (snapshot.hud.laneCount > 1) {
            let west = 0;
            let east = 0;
            for (let r = 0; r < snapshot.grid.rows; r += 1) {
                for (let c = 0; c < snapshot.grid.cols; c += 1) {
                    if (!snapshot.grid.cells[r][c]) {
                        continue;
                    }
                    if (c <= 4) {
                        west += 1;
                    } else {
                        east += 1;
                    }
                }
            }
            this.progress.string = `东片剩余 ${east} · 西片剩余 ${west}`;
        } else {
            this.progress.string = `剩余 ${snapshot.hud.fruitsRemaining}`;
        }
    }
}
