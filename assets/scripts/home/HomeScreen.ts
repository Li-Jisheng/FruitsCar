import { Color, EventTouch, Graphics, Label, Mask, Node, Vec2 } from 'cc';
import { LevelSummary } from '../bridge/GameBridge';
import { color, circle, label, node, rounded } from '../view/UiKit';
import { LevelCard } from './LevelCard';

export interface HomeCallbacks {
    onSelect: (id: string) => void;
    onRetry: () => void;
}

const STATUS_COLORS = {
    pending: '#5A6278',
    online: '#50C88C',
    offline: '#F06A6A',
};

/** 卡片列表的可见区域，单位为设计像素（左上角为原点）。 */
const CARD_BAND_TOP = 240;
const CARD_BAND_HEIGHT = 700;
const CARD_STEP = 330;

/**
 * 主页面（任务书 7.3 / 7.5）：标题区、每个关卡目录条目一张卡片、
 * 连接状态栏和版本号行。
 *
 * 卡片列表位于带遮罩且可拖动滚动的区域内：第三阶段有五个
 * 关卡，列表已无法在一屏内放下。滚动仅用于展示
 * ——它从不改变当前关卡（当前关卡由 host 决定）。
 */
export class HomeScreen {
    readonly root: Node;
    private readonly cards = new Map<string, LevelCard>();
    private readonly viewport: Node;
    private readonly cardLayer: Node;
    private statusDot: Node;
    private readonly statusText: Label;
    private readonly emptyText: Label;
    private readonly retryButton: Node;
    private levelsLoaded = false;
    private scroll = 0;
    private contentBottom = CARD_BAND_TOP;
    private dragging = false;
    private pageUp: Node = new Node();
    private pageDown: Node = new Node();
    private dragTotal = 0;
    private lastTouch = new Vec2();

    constructor(parent: Node, private readonly callbacks: HomeCallbacks) {
        this.root = node(parent, 'HomeScreen', 720, 1280);

        const title = label(this.root, 'Txt_title', 640, 80, 'FruitsCar', 64, color('#E8EEFA'), true);
        title.node.setPosition(0, 640 - 112, 0);
        const subtitle = label(this.root, 'Txt_subtitle', 640, 32, '水果装车 · 选择关卡', 24, color('#8A93A6'));
        subtitle.node.setPosition(0, 640 - 176, 0);

        const divider = node(this.root, 'Divider', 220, 2);
        const graphics = divider.addComponent(Graphics);
        graphics.fillColor = color('#3A4660');
        graphics.rect(-110, -1, 220, 2);
        graphics.fill();
        divider.setPosition(0, 640 - 214, 0);

        // 带遮罩的卡片区域：设计坐标 y 从 CARD_BAND_TOP 到 +CARD_BAND_HEIGHT。
        const bandCentre = CARD_BAND_TOP + CARD_BAND_HEIGHT / 2;
        this.viewport = node(this.root, 'CardViewport', 720, CARD_BAND_HEIGHT);
        this.viewport.setPosition(0, 640 - bandCentre, 0);
        const mask = this.viewport.addComponent(Mask);
        mask.type = Mask.Type.GRAPHICS_RECT;
        // 该层保持 canvas 坐标（两个偏移相互抵消），因此
        // LevelCard 可以继续用 at(x, y) 定位自身。
        this.cardLayer = node(this.viewport, 'Cards', 720, 1280);
        this.cardLayer.setPosition(0, -(640 - bandCentre), 0);
        this.viewport.on(Node.EventType.TOUCH_START, (event: EventTouch) => {
            this.dragging = false;
            this.dragTotal = 0;
            this.lastTouch = event.getUILocation();
        });
        this.viewport.on(Node.EventType.TOUCH_MOVE, (event: EventTouch) => {
            const location = event.getUILocation();
            const deltaY = location.y - this.lastTouch.y;
            this.lastTouch = location;
            this.dragTotal += Math.abs(deltaY) + Math.abs(location.x - this.lastTouch.x);
            if (this.dragTotal > 10) {
                this.dragging = true;
            }
            // 手指上移（负增量）会显示更下方的卡片。
            this.scrollBy(-deltaY);
        });
        this.viewport.on(Node.EventType.TOUCH_END, () => {
            this.dragging = false;
        });
        this.viewport.on(Node.EventType.TOUCH_CANCEL, () => {
            this.dragging = false;
        });

        const statusBar = rounded(this.root, 'StatusBar', 632, 92, color('#161C28'), 16);
        statusBar.setPosition(0, 640 - (960 + 46), 0);

        this.statusDot = circle(statusBar, 'StatusDot', 7, color(STATUS_COLORS.pending));
        this.statusDot.setPosition(-272, 0, 0);
        this.statusText = label(statusBar, 'Txt_status', 480, 30, '正在连接本地服务', 18, color('#C8D2E6'));
        this.statusText.horizontalAlign = Label.HorizontalAlign.LEFT;
        this.statusText.node.setPosition(24, 0, 0);

        this.emptyText = label(this.root, 'Txt_empty', 600, 30, '', 18, color('#F06A6A'));
        this.emptyText.node.setPosition(0, 640 - 930, 0);

        this.retryButton = rounded(this.root, 'Btn_retry', 140, 52, color('#26304A'), 12);
        this.retryButton.setPosition(240, 640 - 1006, 0);
        const retryText = label(this.retryButton, 'Txt_retry', 120, 34, '重试', 20, color('#96B4F0'));
        retryText.node.setPosition(0, 0, 0);
        this.retryButton.on(Node.EventType.TOUCH_END, (_event: EventTouch) => this.callbacks.onRetry());
        this.retryButton.active = false;

        const help = label(this.root, 'Txt_help', 640, 24, '点击卡片进入；上下拖动或用箭头翻页', 15, color('#78829A'));
        help.node.setPosition(0, 640 - 1092, 0);

        // 翻页按钮：仅用指针即可操作，是拖动的可靠替代方案，
        // 也是在编辑器中通过合成输入到达第 3..5 张卡片的唯一方式。
        this.pageUp = this.pageButton('Btn_page_up', '▲', 0, () => this.page(-1));
        this.pageDown = this.pageButton('Btn_page_down', '▼', 1, () => this.page(1));

        const version = label(this.root, 'Txt_version', 640, 22, 'v0.3.0 · 曲线关卡阶段', 14, color('#5A6278'));
        version.node.setPosition(0, 640 - 1216, 0);
    }

    private pageButton(name: string, text: string, side: number,
                       onTap: () => void): Node {
        const button = rounded(this.root, name, 54, 54, color('#26304A'), 12);
        button.setPosition(side === 0 ? -330 : 330, 640 - 930, 0);
        const caption = label(button, 'Txt_' + name, 40, 32, text, 22, color('#96B4F0'));
        caption.node.setPosition(0, 0, 0);
        button.on(Node.EventType.TOUCH_END, (_event: EventTouch) => {
            this.dragging = false;
            onTap();
        });
        return button;
    }

    private page(direction: number): void {
        // direction 为 1 表示查看列表更下方（内容向上移动）。
        this.scrollBy(direction * (CARD_BAND_HEIGHT - 80));
    }

    private maxScroll(): number {
        return Math.max(0, this.contentBottom - (CARD_BAND_TOP + CARD_BAND_HEIGHT) + 20);
    }

    private updatePageButtons(): void {
        this.pageUp.active = this.scroll > 1;
        this.pageDown.active = this.scroll < this.maxScroll() - 1;
    }

    /** scroll > 0 时列表向上移动（显示更下方的卡片）。 */
    private scrollBy(delta: number): void {
        this.scroll = Math.max(0, Math.min(this.maxScroll(), this.scroll + delta));
        const bandCentre = CARD_BAND_TOP + CARD_BAND_HEIGHT / 2;
        this.cardLayer.setPosition(0, -(640 - bandCentre) + this.scroll, 0);
        this.updatePageButtons();
    }

    /** 列表被拖动期间为 true（松开时卡片不得触发）。 */
    get isDragging(): boolean {
        return this.dragging;
    }

    /** 为每个关卡目录条目构建一张卡片（绝不硬编码关卡）。 */
    setLevels(levels: LevelSummary[], currentId: string | null): void {
        for (const [, card] of this.cards) {
            card.root.destroy();
        }
        this.cards.clear();
        this.cardLayer.removeAllChildren();
        this.levelsLoaded = levels.length > 0;
        this.emptyText.string = this.levelsLoaded ? '' : '未找到关卡，请检查 levels/catalogue.json';
        const tops = [CARD_BAND_TOP, CARD_BAND_TOP + CARD_STEP];
        this.contentBottom = CARD_BAND_TOP;
        levels.forEach((level, index) => {
            const top = tops[index] !== undefined
                ? tops[index]
                : CARD_BAND_TOP + index * CARD_STEP;
            const card = new LevelCard(this.cardLayer, level, top, (id) => this.callbacks.onSelect(id),
                () => this.dragging);
            this.cards.set(level.id, card);
            if (currentId === level.id) {
                card.setState('current');
            }
            if (top + 296 > this.contentBottom) {
                this.contentBottom = top + 296;
            }
        });
        this.scroll = 0;
        this.scrollBy(0);
    }

    get loaded(): boolean {
        return this.levelsLoaded;
    }

    hasCard(id: string): boolean {
        return this.cards.has(id);
    }

    get cardCount(): number {
        return this.cards.size;
    }

    setConnection(state: 'pending' | 'online' | 'offline', detail: string): void {
        this.statusText.string = state === 'online'
            ? '本地服务 127.0.0.1:8787 已连接'
            : state === 'offline' ? `未连接本地服务（127.0.0.1:8787）${detail ? ' · ' + detail : ''}` : '正在连接本地服务';
        this.statusText.color = color(state === 'online' ? '#C8D2E6' : '#F06A6A');
        this.retryButton.active = state === 'offline';
        const dotColor = color(state === 'online' ? STATUS_COLORS.online
            : state === 'offline' ? STATUS_COLORS.offline : STATUS_COLORS.pending);
        const parent = this.statusDot.parent;
        if (!parent) {
            return;
        }
        const dot = circle(parent, 'StatusDot', 7, dotColor);
        dot.setPosition(this.statusDot.position.x, this.statusDot.position.y, 0);
        dot.setSiblingIndex(this.statusDot.getSiblingIndex());
        this.statusDot.destroy();
        this.statusDot = dot;
    }

    /** 关卡加载期间，`levelId` 以外的卡片会被禁用。 */
    setLoading(levelId: string | null): void {
        for (const [id, card] of this.cards) {
            card.setState(id === levelId ? 'loading' : 'normal');
        }
    }

    setCurrent(levelId: string | null): void {
        for (const [id, card] of this.cards) {
            card.setState(id === levelId ? 'current' : 'normal');
        }
    }

    setError(levelId: string, code: string): void {
        const card = this.cards.get(levelId);
        if (!card) {
            return;
        }
        card.setState('error', code);
        // 2 秒后错误文本会再次消失（任务书 7.4.2）。
        setTimeout(() => {
            if (this.cards.get(levelId) === card) {
                card.setState('normal');
            }
        }, 2000);
    }

    setVisible(visible: boolean): void {
        this.root.active = visible;
    }
}
