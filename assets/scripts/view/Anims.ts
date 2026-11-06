import { Color, Node, Tween, UIOpacity, Vec3, tween } from 'cc';

/**
 * 仅用于展示的动画库（任务书 5.7 / 7 T-24）。
 *
 * 动画从不反向影响玩法：这里的回调都不会调用 host，
 * 视图层之外的任何代码也不会读取这些值。时长遵循
 * 5.7 中的约定（A-4 / A-5）。
 */
export const ANIM = {
    /** A-4：水果缩放变化。 */
    scale: 0.15,
    /** A-4：收集 = 脉冲放大到 1.15 倍，然后飞入车内。 */
    collectPulse: 0.1,
    collectFly: 0.25,
    /** A-5：车辆进入 / 离开。 */
    enter: 0.2,
    exit: 0.25,
    /** A-3：车辆显示位置的最大追赶时间。 */
    chase: 0.12,
};

function opacityOf(node: Node): UIOpacity {
    let opacity = node.getComponent(UIOpacity);
    if (!opacity) {
        opacity = node.addComponent(UIOpacity);
    }
    return opacity;
}

/** 将 node 补间到目标位置（A-3 插值目标）。 */
export function tweenTo(node: Node, target: Vec3, duration = ANIM.chase, onDone?: () => void): void {
    if (!node.isValid) {
        return;
    }
    Tween.stopAllByTarget(node);
    tween(node)
        .to(duration, { position: target }, { easing: 'quadOut' })
        .call(() => onDone?.())
        .start();
}

/** 水果被收集时使用的缩放脉冲。 */
export function pulse(node: Node, scale = 1.15, duration = ANIM.collectPulse): void {
    if (!node.isValid) {
        return;
    }
    const base = node.scale.clone();
    tween(node)
        .to(duration, { scale: new Vec3(base.x * scale, base.y * scale, 1) }, { easing: 'quadOut' })
        .to(duration, { scale: base }, { easing: 'quadIn' })
        .start();
}

/** 将 node 飞向目标，然后执行 `onDone`（收集入车、补充滑动）。 */
export function flyTo(node: Node, target: Vec3, duration = ANIM.collectFly, onDone?: () => void): void {
    if (!node.isValid) {
        return;
    }
    Tween.stopAllByTarget(node);
    tween(node)
        .to(duration, { position: target }, { easing: 'quadIn' })
        .call(() => onDone?.())
        .start();
}

/** 淡入或淡出 node，可选择在之后销毁它。 */
export function fade(node: Node, to: number, duration: number, onDone?: () => void): void {
    if (!node.isValid) {
        return;
    }
    const opacity = opacityOf(node);
    tween(opacity)
        .to(duration, { opacity: to })
        .call(() => onDone?.())
        .start();
}

/** 使用自定义时长的缩放补间（单个水果的尺寸变化，A-4）。 */
export function tweenScale(node: Node, scale: number, duration = ANIM.scale): void {
    if (!node.isValid) {
        return;
    }
    tween(node)
        .to(duration, { scale: new Vec3(scale, scale, 1) }, { easing: 'quadOut' })
        .start();
}

/** 收集闪光颜色的便捷常量（仅用于视图）。 */
export const FLASH = new Color(255, 245, 200, 255);
