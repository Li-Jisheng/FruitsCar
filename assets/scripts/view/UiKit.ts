import { Color, Graphics, Label, Node, UITransform, Vec3 } from 'cc';

/**
 * 主页面和游戏页共用的布局辅助函数。
 *
 * 任务书（7.3）以 720x1280 画布的左上角为原点给出每个
 * 坐标；Cocos 使用中心原点且 y 轴向上。`px`/`py` 是进行
 * 该转换的唯一位置。
 */
export const DESIGN_WIDTH = 720;
export const DESIGN_HEIGHT = 1280;

export function px(x: number): number {
    return x - DESIGN_WIDTH / 2;
}

export function py(y: number): number {
    return DESIGN_HEIGHT / 2 - y;
}

export function at(x: number, y: number): Vec3 {
    return new Vec3(px(x), py(y), 0);
}

export function color(hex: string, alpha = 255): Color {
    const text = hex.replace('#', '');
    const value = parseInt(text.length === 3
        ? text.split('').map((c) => c + c).join('')
        : text, 16);
    return new Color((value >> 16) & 0xff, (value >> 8) & 0xff, value & 0xff, alpha);
}

export function node(parent: Node, name: string, width: number, height: number): Node {
    const created = new Node(name);
    created.setParent(parent);
    const transform = created.addComponent(UITransform);
    transform.setAnchorPoint(0.5, 0.5);
    transform.setContentSize(width, height);
    return created;
}

function graphicsOf(target: Node): Graphics {
    let graphics = target.getComponent(Graphics);
    if (!graphics) {
        graphics = target.addComponent(Graphics);
    }
    graphics.clear();
    return graphics;
}

/** 纯色填充矩形。 */
export function block(parent: Node, name: string, width: number, height: number, fill: Color): Node {
    const created = node(parent, name, width, height);
    const graphics = graphicsOf(created);
    graphics.fillColor = fill;
    graphics.rect(-width / 2, -height / 2, width, height);
    graphics.fill();
    return created;
}

/** 可选 2px 边框的圆角矩形（任务书 4.7 / 7.3）。 */
export function rounded(parent: Node, name: string, width: number, height: number, fill: Color,
                        radius = 0, border?: Color, borderWidth = 2): Node {
    const created = node(parent, name, width, height);
    const graphics = graphicsOf(created);
    graphics.fillColor = fill;
    if (radius > 0) {
        graphics.roundRect(-width / 2, -height / 2, width, height, radius);
    } else {
        graphics.rect(-width / 2, -height / 2, width, height);
    }
    graphics.fill();
    if (border) {
        graphics.lineWidth = borderWidth;
        graphics.strokeColor = border;
        if (radius > 0) {
            graphics.roundRect(-width / 2, -height / 2, width, height, radius);
        } else {
            graphics.rect(-width / 2, -height / 2, width, height);
        }
        graphics.stroke();
    }
    return created;
}

/** 用水平色带近似垂直渐变（主页背景）。 */
export function gradient(parent: Node, name: string, width: number, height: number, top: Color,
                         bottom: Color, bands = 48): Node {
    const created = node(parent, name, width, height);
    const graphics = graphicsOf(created);
    const step = height / bands;
    for (let i = 0; i < bands; i += 1) {
        const t = bands <= 1 ? 0 : i / (bands - 1);
        graphics.fillColor = new Color(
            Math.round(top.r + (bottom.r - top.r) * t),
            Math.round(top.g + (bottom.g - top.g) * t),
            Math.round(top.b + (bottom.b - top.b) * t),
            Math.round(top.a + (bottom.a - top.a) * t));
        graphics.rect(-width / 2, height / 2 - (i + 1) * step, width, step + 0.6);
        graphics.fill();
    }
    return created;
}

/** 圆形（难度圆点、状态灯、序号徽章）。 */
export function circle(parent: Node, name: string, radius: number, fill: Color): Node {
    const created = node(parent, name, radius * 2, radius * 2);
    const graphics = graphicsOf(created);
    graphics.fillColor = fill;
    graphics.circle(0, 0, radius);
    graphics.fill();
    return created;
}

/** 沿 `angleDeg` 指向的三角形（0 = 向上，90 = 向右）。 */
export function triangle(parent: Node, name: string, size: number, fill: Color, angleDeg = 0): Node {
    const created = node(parent, name, size, size);
    const graphics = graphicsOf(created);
    graphics.fillColor = fill;
    graphics.moveTo(0, size / 2);
    graphics.lineTo(size / 2, -size / 2);
    graphics.lineTo(-size / 2, -size / 2);
    graphics.close();
    graphics.fill();
    created.angle = -angleDeg;
    return created;
}

export function label(parent: Node, name: string, width: number, height: number, text: string,
                      fontSize: number, fill: Color, bold = false): Label {
    const created = node(parent, name, width, height);
    const component = created.addComponent(Label);
    component.string = text;
    component.fontSize = fontSize;
    component.lineHeight = fontSize + 4;
    component.color = fill;
    component.isBold = bold;
    component.horizontalAlign = Label.HorizontalAlign.CENTER;
    component.verticalAlign = Label.VerticalAlign.CENTER;
    component.overflow = Label.Overflow.SHRINK;
    return component;
}

export function leftLabel(parent: Node, name: string, width: number, height: number, text: string,
                          fontSize: number, fill: Color, bold = false): Label {
    const component = label(parent, name, width, height, text, fontSize, fill, bold);
    component.horizontalAlign = Label.HorizontalAlign.LEFT;
    return component;
}
