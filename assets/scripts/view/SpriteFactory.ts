import { Color, Graphics, Label, Node, UITransform } from 'cc';
import { color as hex, node as uiNode } from './UiKit';

/**
 * 第 1/2 阶段不使用图片资源：每个方块都是填充纯色的
 * Graphics 矩形（任务书 3.4 允许“1x1 Sprite 或 Graphics”）。Graphics
 * 完全避开了运行时纹理/动态图集的路径，因为该路径在
 * 编辑器 Game View 预览中很不稳定。
 */
export function paletteColor(colorId: string): Color {
    switch (colorId) {
        case 'red':
            return new Color(226, 87, 76, 255);
        case 'yellow':
            return new Color(242, 193, 78, 255);
        case 'blue':
            return new Color(91, 141, 239, 255);
        case 'purple':
            return new Color(160, 92, 208, 255);
        case 'green':
            return new Color(72, 186, 116, 255);
        default:
            return new Color(200, 200, 200, 255);
    }
}

export { uiNode, hex };

function paint(node: Node, fill: Color, radius = 0, highlight = false): void {
    const transform = node.getComponent(UITransform);
    if (!transform) {
        return;
    }
    const width = transform.contentSize.width;
    const height = transform.contentSize.height;
    let graphics = node.getComponent(Graphics);
    if (!graphics) {
        graphics = node.addComponent(Graphics);
    }
    graphics.clear();
    graphics.fillColor = fill;
    if (radius > 0) {
        graphics.roundRect(-width / 2, -height / 2, width, height, radius);
    } else {
        graphics.rect(-width / 2, -height / 2, width, height);
    }
    graphics.fill();
    if (highlight) {
        // “左上高光”：方块左上角的一层较亮覆盖。
        graphics.fillColor = new Color(
            Math.min(255, fill.r + 34), Math.min(255, fill.g + 34), Math.min(255, fill.b + 34), 210);
        graphics.roundRect(-width / 2 + width * 0.12, height * 0.04, width * 0.36, height * 0.2, radius > 0 ? radius * 0.5 : 0);
        graphics.fill();
    }
}

export function addBlock(parent: Node, name: string, width: number, height: number, fill: Color): Node {
    const created = uiNode(parent, name, width, height);
    paint(created, fill);
    return created;
}

export function addRoundBlock(parent: Node, name: string, width: number, height: number, fill: Color,
                              radius = 0, highlight = false): Node {
    const created = uiNode(parent, name, width, height);
    paint(created, fill, radius, highlight);
    return created;
}

export function setBlockColor(target: Node, fill: Color, radius = 0): void {
    paint(target, fill, radius, false);
}

export function setFruitColor(target: Node, fill: Color, radius: number): void {
    paint(target, fill, radius, true);
}

export function addLabel(parent: Node, name: string, width: number, height: number, text: string,
                         fontSize: number, fill: Color = new Color(255, 255, 255, 255)): Label {
    const created = uiNode(parent, name, width, height);
    const component = created.addComponent(Label);
    component.string = text;
    component.fontSize = fontSize;
    component.lineHeight = fontSize + 4;
    component.color = fill;
    component.horizontalAlign = Label.HorizontalAlign.CENTER;
    component.verticalAlign = Label.VerticalAlign.CENTER;
    component.overflow = Label.Overflow.SHRINK;
    return component;
}

export function createLayer(parent: Node, name: string): Node {
    return uiNode(parent, name, 720, 1280);
}
