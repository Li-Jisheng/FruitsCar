import { Color, EventTouch, Label, Node } from 'cc';
import { LevelSummary } from '../bridge/GameBridge';
import { circle, color, label, node, rounded, at } from '../view/UiKit';
import { Thumbnail } from './Thumbnail';

export type CardState = 'normal' | 'pressed' | 'loading' | 'current' | 'error';

const CARD_WIDTH = 632;
const CARD_HEIGHT = 296;

/**
 * 关卡卡片（任务书 7.4）：序号徽章、名称、副标题、机制标签、
 * 难度圆点和实时缩略图。所有文本都来自关卡目录。
 */
export class LevelCard {
    readonly root: Node;
    private border: Node;
    private readonly loadingMask: Node;
    private readonly errorLabel: Label;
    private readonly currentBadge: Label;
    private readonly body: Node;
    private state: CardState = 'normal';

    constructor(parent: Node, readonly level: LevelSummary, top: number, onSelect: (id: string) => void,
                private readonly isDragging: () => boolean = () => false) {
        this.root = node(parent, `Card_${level.id}`, CARD_WIDTH, CARD_HEIGHT);
        // 卡片按左上角定位（任务书 7.3）：(44, top)。
        this.root.setPosition(at(44 + CARD_WIDTH / 2, top + CARD_HEIGHT / 2));

        this.body = rounded(this.root, 'CardBg', CARD_WIDTH, CARD_HEIGHT, color('#1C2332'), 30);
        this.body.setPosition(0, 0, 0);
        this.border = rounded(this.root, 'CardBorder', CARD_WIDTH, CARD_HEIGHT, new Color(0, 0, 0, 0), 30,
            color('#34405A'), 2);
        this.border.setPosition(0, 0, 0);

        // 子节点使用卡片局部坐标：(0,0) 为卡片中心。
        const localX = (x: number): number => x - (44 + CARD_WIDTH / 2);
        const localY = (y: number): number => (top + CARD_HEIGHT / 2) - y;

        const badge = circle(this.root, 'NumberBadge', 50, color(level.accent || '#F2C14E'));
        badge.setPosition(localX(72 + 50), localY(top + 34 + 50), 0);
        const number = label(badge, 'Txt_number', 90, 60, level.order < 10 ? `0${level.order}` : `${level.order}`, 40,
            color('#10141C'), true);
        number.node.setPosition(0, 0, 0);

        const name = label(this.root, 'Txt_name', 260, 48, level.name, 36, color('#EEF2FA'), true);
        name.horizontalAlign = Label.HorizontalAlign.LEFT;
        name.node.setPosition(localX(196 + 130), localY(top + 44 + 24), 0);

        const subtitle = label(this.root, 'Txt_subtitle', 260, 28, level.subtitle, 19, color('#A0AABE'));
        subtitle.horizontalAlign = Label.HorizontalAlign.LEFT;
        subtitle.node.setPosition(localX(196 + 130), localY(top + 96 + 14), 0);

        const tag = rounded(this.root, 'Tag', 96, 34, color('#26304A'), 8);
        tag.setPosition(localX(196 + 48), localY(top + 132 + 17), 0);
        const tagText = label(tag, 'Txt_tag', 92, 26, level.tag, 16, color('#96B4F0'));
        tagText.node.setPosition(0, 0, 0);

        for (let i = 0; i < 3; i += 1) {
            const dot = circle(this.root, `Difficulty_${i}`, 9,
                i < level.difficulty ? color(level.accent || '#F2C14E') : color('#3A4254'));
            dot.setPosition(localX(210 + 30 * i), localY(top + 206), 0);
        }

        const hint = label(this.root, 'Txt_enter', 260, 24, '点击卡片开始', 17, color('#8CC8A0'));
        hint.node.setPosition(localX(300 + 130), localY(top + 250), 0);

        const thumb = new Thumbnail(this.root, level.preview, 168);
        thumb.root.setPosition(localX(456 + 84), localY(top + 40 + 84), 0);

        this.currentBadge = label(this.root, 'Txt_current', 120, 24, '进行中', 16, color(level.accent || '#F2C14E'));
        this.currentBadge.node.setPosition(localX(44 + CARD_WIDTH - 104), localY(top + 26), 0);
        this.currentBadge.node.active = false;

        this.loadingMask = rounded(this.root, 'LoadingMask', CARD_WIDTH, CARD_HEIGHT, new Color(6, 9, 14, 150), 30);
        this.loadingMask.setPosition(0, 0, 0);
        const loadingText = label(this.loadingMask, 'Txt_loading', 240, 40, '载入中…', 26, color('#E8EEFA'), true);
        loadingText.node.setPosition(0, 0, 0);
        this.loadingMask.active = false;

        this.errorLabel = label(this.root, 'Txt_error', 300, 24, '', 16, color('#F06A6A'));
        this.errorLabel.node.setPosition(localX(196 + 150), localY(top + 250), 0);

        this.root.on(Node.EventType.TOUCH_START, (_event: EventTouch) => {
            if (this.state === 'normal' || this.state === 'current') {
                this.root.setScale(0.98, 0.98, 1);
            }
        });
        this.root.on(Node.EventType.TOUCH_CANCEL, (_event: EventTouch) => this.root.setScale(1, 1, 1));
        this.root.on(Node.EventType.TOUCH_END, (_event: EventTouch) => {
            this.root.setScale(1, 1, 1);
            if (this.state === 'loading' || this.state === 'error') {
                return;
            }
            if (this.isDragging()) {
                return;
            }
            onSelect(this.level.id);
        });
    }

    get levelId(): string {
        return this.level.id;
    }

    setState(next: CardState, errorText = ''): void {
        this.state = next;
        this.loadingMask.active = next === 'loading';
        this.currentBadge.node.active = next === 'current';
        this.errorLabel.string = next === 'error' ? errorText : '';
        const borderColor = next === 'current' ? color(this.level.accent || '#F2C14E')
            : next === 'error' ? color('#F06A6A') : color('#34405A');
        const replacement = rounded(this.root, 'CardBorder', CARD_WIDTH, CARD_HEIGHT, new Color(0, 0, 0, 0), 30,
            borderColor, 2);
        replacement.setPosition(0, 0, 0);
        replacement.setSiblingIndex(1);
        this.border.destroy();
        this.border = replacement;
        if (next === 'loading') {
            this.loadingMask.setSiblingIndex(this.root.children.length - 1);
        }
    }
}
