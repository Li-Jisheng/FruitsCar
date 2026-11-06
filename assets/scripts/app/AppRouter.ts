import { Node } from 'cc';
import { GameBridge, LevelSummary, LevelsResponse, Snapshot } from '../bridge/GameBridge';
import { HomeScreen } from '../home/HomeScreen';
import { GameView } from '../view/GameView';

export type PageState = 'boot' | 'home' | 'loadingLevel' | 'game' | 'result' | 'error';

const SELECT_TIMEOUT_MS = 3000;

/**
 * 单场景双屏 + 常驻 AppRouter（任务书 7.2 / 7.5，D-P2-11）。
 *
 * 它负责页面状态机：客户端从不决定当前关卡
 * （由 host 决定），也从不决定结果（由快照决定）。
 */
export class AppRouter {
    private readonly home: HomeScreen;
    private readonly game: GameView;
    private state: PageState = 'boot';
    private levels: LevelSummary[] = [];
    private currentId: string | null = null;
    private pendingId: string | null = null;
    private pendingEpoch = -1;
    private pendingSince = 0;
    private currentLevel: LevelSummary | null = null;

    constructor(private readonly root: Node, private readonly bridge: GameBridge, callbacks: {
        onFeedback: (message: string, accepted: boolean) => void;
    }) {
        this.home = new HomeScreen(root, {
            onSelect: (id) => this.selectLevel(id),
            onRetry: () => this.reloadLevels(),
        });
        this.game = new GameView(root, bridge, {
            onBack: () => this.backHome(),
            onRestart: () => this.restart(),
            onHome: () => this.backHome(),
            onFeedback: callbacks.onFeedback,
        });
        this.home.setVisible(true);
        this.game.setVisible(false);
        this.home.setConnection('pending', '');
        this.state = 'home';
    }

    get page(): { state: PageState; levelId: string | null; epoch: number; cards: number } {
        return {
            state: this.state,
            levelId: this.currentId,
            epoch: this.pendingEpoch,
            cards: this.home.cardCount,
        };
    }

    /** 启动流程：GET /levels，然后显示卡片（S1 -> S2）。 */
    async reloadLevels(): Promise<void> {
        try {
            const response: LevelsResponse = await this.bridge.fetchLevels();
            this.levels = response.levels ?? [];
            this.currentId = response.current ? response.current.id : null;
            this.home.setLevels(this.levels, this.currentId);
            this.home.setConnection('online', '');
            this.state = 'home';
            if (this.levels.length === 0) {
                this.home.setVisible(true);
            }
        } catch (error) {
            this.home.setConnection('offline', error instanceof Error ? error.message : '');
            this.state = 'error';
        }
    }

    /** S2 -> S3：POST /level/select，然后等待匹配的首帧。 */
    private async selectLevel(id: string): Promise<void> {
        if (this.state === 'loadingLevel') {
            return;
        }
        const summary = this.levels.find((level) => level.id === id) ?? null;
        if (!summary) {
            return;
        }
        this.state = 'loadingLevel';
        this.pendingId = id;
        this.pendingEpoch = -1;
        this.pendingSince = Date.now();
        this.home.setLoading(id);
        try {
            const result = await this.bridge.selectLevel(id);
            if (!result.ok) {
                this.state = 'home';
                this.pendingId = null;
                this.home.setLoading(null);
                this.home.setError(id, result.error === 'UNKNOWN_LEVEL' ? 'E1019' : (result.error ?? 'E1019'));
                return;
            }
            this.pendingEpoch = result.levelEpoch ?? 0;
            this.currentLevel = summary;
            this.game.setLevel(summary);
            // 首个 (levelEpoch, levelId) 匹配的快照会切换页面。
            const snapshot = this.bridge.snapshot;
            if (snapshot && snapshot.levelEpoch === this.pendingEpoch && snapshot.levelId === id) {
                this.enterGame(snapshot);
            }
        } catch (error) {
            this.state = 'home';
            this.pendingId = null;
            this.home.setLoading(null);
            this.home.setError(id, error instanceof Error ? error.message : 'E1019');
        }
    }

    private enterGame(snapshot: Snapshot): void {
        this.state = 'game';
        this.pendingId = null;
        this.currentId = snapshot.levelId;
        this.home.setLoading(null);
        this.home.setVisible(false);
        this.game.setVisible(true);
        this.game.applySnapshot(snapshot);
    }

    /** 任意快照：游戏进行中时路由到游戏页，否则停留在主页面。 */
    onSnapshot(snapshot: Snapshot): void {
        if (this.state === 'loadingLevel' && this.pendingId !== null) {
            const matches = snapshot.levelEpoch === this.pendingEpoch && snapshot.levelId === this.pendingId;
            const timedOut = Date.now() - this.pendingSince > SELECT_TIMEOUT_MS;
            if (matches) {
                this.enterGame(snapshot);
            } else if (timedOut) {
                this.state = 'home';
                const failed = this.pendingId;
                this.pendingId = null;
                this.home.setLoading(null);
                if (failed) {
                    this.home.setError(failed, 'TIMEOUT');
                }
            }
            return;
        }
        if (this.state !== 'game' && this.state !== 'result') {
            return;
        }
        this.game.applySnapshot(snapshot);
        if (snapshot.hud.result !== null && this.state === 'game') {
            this.state = 'result';
        } else if (snapshot.hud.result === null && this.state === 'result') {
            this.state = 'game';
        }
    }

    /** S4 -> S2：返回主页面并暂停 host（任务书 7.5）。 */
    private async backHome(): Promise<void> {
        try {
            await this.bridge.setPaused(true);
        } catch (error) {
            // 主页面仍会打开；状态栏会报告失败。
        }
        this.state = 'home';
        this.game.setVisible(false);
        this.home.setVisible(true);
        this.home.setCurrent(this.currentId);
        await this.reloadLevels();
        this.home.setCurrent(this.currentId);
    }

    /** S5 -> S3：重开当前关卡（id 相同，epoch 全新）。 */
    private async restart(): Promise<void> {
        const id = this.currentId;
        if (!id) {
            return;
        }
        this.state = 'home';
        await this.selectLevel(id);
    }

    onConnection(connected: boolean, detail: string): void {
        if (connected) {
            // host 恢复后必须修复过早发生的启动
            // （任务书 7.8：“恢复后自动继续”）。
            if (this.levels.length === 0) {
                this.reloadLevels().catch(() => undefined);
                return;
            }
            if (this.state === 'home') {
                this.home.setConnection('online', '');
            }
            return;
        }
        if (this.state === 'home' || this.state === 'boot') {
            this.home.setConnection('offline', detail);
        }
    }

    get gameView(): GameView {
        return this.game;
    }

    get homeScreen(): HomeScreen {
        return this.home;
    }
}
