import { Color, Label, Node, UITransform, Vec3 } from 'cc';
import { ANIM, fade, tweenTo } from './Anims';
import { addBlock, addLabel, paletteColor, setBlockColor, uiNode } from './SpriteFactory';

/**
 * 一辆道路车辆（任务书 7 T-22）。
 *
 * 视图维护自己的显示位置，并按任务书 5.7 A-3 的约定
 * 追赶快照位置：`displayS` 跟随 `speed`，每帧步长
 * 不超过 `2 * speed * dt`，接近目标后车辆直接吸附。
 * 这里的内容不会被核心读取；关闭动画只会改变像素。
 */
export class CarView {
    readonly node: Node;
    private readonly body: Node;
    private readonly label: Label;
    private target = new Vec3();
    private targetAngle = 0;
    private hasTarget = false;
    private currentAngle = 0;
    private pendingEnter = false;

    constructor(parent: Node, readonly id: string, width: number, height: number, color: string,
                text: string) {
        this.node = uiNode(parent, `Car_${id}`, width, height);
        this.body = addBlock(this.node, 'Body', width, height, paletteColor(color));
        this.body.setPosition(0, 0, 0);
        this.label = addLabel(this.node, 'Txt_load', width, 16, text, 13, new Color(20, 22, 28, 255));
        this.label.node.setPosition(0, 0, 0);
    }

    setSize(width: number, height: number): void {
        const transform = this.node.getComponent(UITransform);
        if (transform) {
            transform.setContentSize(width, height);
        }
        const bodyTransform = this.body.getComponent(UITransform);
        if (bodyTransform) {
            bodyTransform.setContentSize(width, height);
        }
    }

    setLabel(text: string): void {
        this.label.string = text;
    }

    setColor(color: string, waiting: boolean): void {
        const base = waiting ? new Color(250, 140, 120, 255) : paletteColor(color);
        const height = this.body.getComponent(UITransform)?.contentSize.height ?? 20;
        setBlockColor(this.body, base, height * 0.35);
    }

    /** 新的快照目标；`entering` 会播放一次滑入动画。 */
    setTarget(position: Vec3, headingDeg: number, immediate = false): void {
        this.target = position.clone();
        this.targetAngle = -headingDeg;
        if (!this.hasTarget || immediate) {
            this.node.setPosition(this.target);
            this.currentAngle = this.targetAngle;
            this.node.angle = this.currentAngle;
            this.hasTarget = true;
            if (immediate) {
                this.pendingEnter = true;
            }
        }
        this.label.node.angle = headingDeg;
    }

    /** A-3：在严格的每帧步长限制下追赶。 */
    update(dt: number, maxStepPx: number, angleStepDeg: number): void {
        if (!this.hasTarget) {
            return;
        }
        const current = this.node.position;
        const delta = new Vec3(this.target.x - current.x, this.target.y - current.y, 0);
        const distance = Math.hypot(delta.x, delta.y);
        if (distance <= 0.5 || distance <= maxStepPx) {
            this.node.setPosition(this.target);
        } else if (maxStepPx > 0) {
            const scale = maxStepPx / distance;
            this.node.setPosition(current.x + delta.x * scale, current.y + delta.y * scale, 0);
        }
        const angleDelta = this.shortestAngle(this.targetAngle - this.currentAngle);
        const step = Math.min(Math.abs(angleDelta), angleStepDeg * dt);
        if (Math.abs(angleDelta) > 0.1) {
            this.currentAngle += Math.sign(angleDelta) * step;
        } else {
            this.currentAngle = this.targetAngle;
        }
        this.node.angle = this.currentAngle;
    }

    private shortestAngle(delta: number): number {
        let value = delta % 360;
        if (value > 180) {
            value -= 360;
        }
        if (value < -180) {
            value += 360;
        }
        return value;
    }

    /** A-5：从入口点后方滑入并淡入。 */
    playEnter(from: Vec3): void {
        if (!this.pendingEnter) {
            return;
        }
        this.pendingEnter = false;
        this.node.setPosition(from);
        fade(this.node, 255, ANIM.enter);
        tweenTo(this.node, this.target, ANIM.enter);
    }

    playExit(onDone: () => void): void {
        fade(this.node, 0, ANIM.exit, () => {
            onDone();
            this.destroy();
        });
    }

    /** 收集脉冲：车辆吞入水果时会短暂放大。 */
    pulse(): void {
        const base = this.node.scale.clone();
        this.node.setScale(base.x * 1.06, base.y * 1.06, 1);
        setTimeout(() => {
            if (this.node.isValid) {
                this.node.setScale(base.x, base.y, 1);
            }
        }, 90);
    }

    position(): Vec3 {
        return this.node.position.clone();
    }

    destroy(): void {
        if (this.node.isValid) {
            this.node.destroy();
        }
    }
}
