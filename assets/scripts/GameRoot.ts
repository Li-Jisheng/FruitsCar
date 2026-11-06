import { Component, Node, ResolutionPolicy, UITransform, _decorator, view } from 'cc';
import { AppRouter } from './app/AppRouter';
import { GameBridge } from './bridge/GameBridge';

const { ccclass, property } = _decorator;

/**
 * 客户端入口。场景中只有一个 Canvas 并挂载此组件；
 * AppRouter 在其中构建主页面和游戏页（单场景、
 * 双页面，D-P2-11）。所有玩法逻辑都位于本地
 * host 背后的 C++ 核心中：此组件只轮询快照并转发输入。
 */
@ccclass('GameRoot')
export class GameRoot extends Component {
    @property({ tooltip: 'Host base URL' })
    hostUrl = 'http://127.0.0.1:8787';

    @property({ tooltip: 'Snapshot poll interval in ms' })
    pollMs = 80;

    private bridge: GameBridge | null = null;
    private router: AppRouter | null = null;

    start(): void {
        try {
            this.bootstrap();
        } catch (error) {
            this.reportBootError('start', error);
            throw error;
        }
    }
    /** 将启动失败上报到 host 日志（仅用于诊断，不影响玩法）。 */
    private reportBootError(stage: string, error: unknown): void {
        const message = error instanceof Error
            ? `${error.name}: ${error.message}\n${error.stack ?? ''}`
            : String(error);
        console.log('[FC_BOOT_ERROR]' + stage + ' ' + message);
        try {
            const target = this.hostUrl || 'http://127.0.0.1:8787';
            fetch(`${target}/client_report`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ check: 'boot_error', stage, message }),
            }).catch(() => undefined);
        } catch {
            // 诊断上报尽力而为。
        }
    }

    private bootstrap(): void {
        view.setDesignResolutionSize(720, 1280, ResolutionPolicy.SHOW_ALL);
        const transform = this.node.getComponent(UITransform);
        if (transform) {
            transform.setContentSize(720, 1280);
        }
        const canvasTransform = this.node.parent?.getComponent(UITransform);
        console.log('[FC_VIEW]' + JSON.stringify({
            design: view.getDesignResolutionSize(),
            visible: view.getVisibleSize(),
            frame: view.getFrameSize(),
            canvas: canvasTransform ? canvasTransform.contentSize : null,
            parent: this.node.parent ? this.node.parent.name : 'none',
        }));
        const bridge = new GameBridge(this.hostUrl, this.pollMs,
            (snapshot) => this.router?.onSnapshot(snapshot),
            (connected, detail) => this.router?.onConnection(connected, detail));
        this.bridge = bridge;
        const router = new AppRouter(this.node, bridge, {
            onFeedback: () => undefined,
        });
        this.router = router;
        (globalThis as unknown as Record<string, unknown>).__fcBridge = bridge;
        (globalThis as unknown as Record<string, unknown>).__fcRouter = router;
        (globalThis as unknown as Record<string, unknown>).__fcGameRoot = this;
        bridge.start();
        router.reloadLevels().catch(() => undefined);
    }

    onDestroy(): void {
        if (this.bridge) {
            this.bridge.stop();
        }
    }

    /** 供 M18/M19 截图证据使用的只读探针。 */
    page(): Record<string, unknown> {
        return this.router ? this.router.page : { state: 'none' };
    }

    /** 供 M18/M19 一致性检查使用的只读探针。 */
    consistency(): Record<string, unknown> {
        return this.router ? this.router.gameView.consistency() : { ok: false, reason: 'no router' };
    }

    /** 用于证据日志的主页面状态快照。 */
    homeState(): Record<string, unknown> {
        if (!this.router) {
            return { ok: false };
        }
        return {
            ok: true,
            cards: this.router.homeScreen.cardCount,
            connected: this.bridge ? this.bridge.connected : false,
            page: this.router.page,
        };
    }

    /** 供证据脚本点击主页卡片（玩家不会使用）。 */
    debugOpenLevel(levelId: string): boolean {
        if (!this.router) {
            return false;
        }
        const node = this.findCardNode(levelId);
        if (!node) {
            return false;
        }
        node.emit(Node.EventType.TOUCH_END);
        return true;
    }

    private findCardNode(levelId: string): Node | null {
        const home = this.router?.homeScreen;
        if (!home) {
            return null;
        }
        const stack: Node[] = [home.root];
        while (stack.length > 0) {
            const current = stack.pop() as Node;
            if (current.name === `Card_${levelId}`) {
                return current;
            }
            for (const child of current.children) {
                stack.push(child);
            }
        }
        return null;
    }
}
// 构建版本提升：第二阶段
