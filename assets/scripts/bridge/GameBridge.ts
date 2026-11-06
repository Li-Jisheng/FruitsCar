/** 快照 / 关卡目录协议类型（与 core/include/fc/serialize.cpp 保持一致）。 */

export interface GridCell {
    fruit: string;
    settle: number;
    ring: number;
    size: number;
}

/** 曲线关卡的扁平水果条目（任务书附录 B）。 */
export interface SnapshotFruit {
    id: number;
    fruit: string;
    x: number;
    y: number;
    size: number;
    settle: number;
    layer: number;
    roadS: number;
    roadD: number;
}

/** 预留的技能状态（`items[]`，任务书 5.8）。 */
export interface SnapshotItem {
    id: string;
    kind: string;
    charges: number;
}

export interface SnapshotEffect {
    kind: string;
    start: number;
    expire: number;
    target: string;
}

export interface SnapshotVehicle {
    id: string;
    color: string;
    load: number;
    capacity: number;
    s: string;
    lane: number;
    pos: [number, number];
    heading: number;
    cell: [number, number];
    state: string;
    label: string;
    x?: number;
    y?: number;
}

export interface SnapshotSlot {
    index: number;
    vehicle: string;
    color: string;
    load: number;
    capacity: number;
    label: string;
}

export interface SnapshotLane {
    index: number;
    color: string;
    queue: string[];
    count: number;
}

export interface RoadLane {
    lane: number;
    collectSide: string;
    cells: [number, number][];
    contacts: [number, number][][];
}

export interface SnapshotRoad {
    length: number | string;
    cells: [number, number][];
    track: [number, number][];
    collectSide: string;
    width: number;
    laneOffset: number;
    laneCells: [number, number][];
    laneCycle: number[];
    nextLane: number;
    lanes: RoadLane[];
    speed?: number;
    carLength?: number;
    /** 仅曲线关卡（任务书 5.2）：采样后的中心线。 */
    polyline?: [number, number][];
    sampleCount?: number;
    sampleStep?: number;
    offset?: number;
}

export interface VehicleInfo {
    color: string;
    load: number;
    capacity: number;
    label: string;
    where: string;
}

export interface SnapshotHud {
    levelName: string;
    fruitsTotal: number;
    fruitsRemaining: number;
    fruitsCollected: number;
    spawnCooldownMs: number;
    requireSelect: boolean;
    result: string | null;
    resultTick: number | null;
    nextAllowedTick: number;
    exitedCount: number;
    parkedCount: number;
    nextLane: number;
    laneCount: number;
    layerCount?: number;
    itemCharges?: number;
    world?: string;
}

export interface SnapshotEvent {
    seq: number;
    tick: number;
    type: string;
    reason?: string;
    car?: string;
    fruit?: string;
    at?: [number, number];
    from?: [number, number];
    lane?: number;
    load?: number;
    capacity?: number;
    sizeFrom?: number;
    sizeTo?: number;
    ring?: number;
    count?: number;
    /** 曲线关卡：稳定的水果标识和世界坐标（x = col，y = row）。 */
    id?: number;
    fromId?: number;
    x?: number;
    y?: number;
    fromX?: number;
    fromY?: number;
    /** 预留的技能事件。 */
    item?: string;
    command?: string;
}

export interface Snapshot {
    seq: number;
    tick: number;
    elapsedMs: number;
    eventSeq: number;
    levelId: string;
    levelEpoch: number;
    paused: boolean;
    seed: number;
    buildVersion: string;
    status: string;
    /** 曲线关卡为 "curves"（网格关卡中不存在）。 */
    world?: string;
    grid: { rows: number; cols: number; cells: (GridCell | null)[][] };
    /** 曲线关卡的扁平水果列表。 */
    fruits?: SnapshotFruit[];
    road: SnapshotRoad;
    vehicles: SnapshotVehicle[];
    parking: { slots: (SnapshotSlot | null)[] };
    lanes: SnapshotLane[];
    vehicleInfo: Record<string, VehicleInfo>;
    items?: SnapshotItem[];
    effects?: SnapshotEffect[];
    hud: SnapshotHud;
    events: SnapshotEvent[];
}

export interface CommandReceipt {
    accepted: boolean;
    reason: string;
    nextAllowedTick: number;
}

export interface RingPreview {
    /** 环形中心的世界坐标：[x = col，y = row]。 */
    centre: [number, number];
    sectors: number;
    startDeg: number;
    sweepDeg: number;
    innerRadius: number;
    outerRadius: number;
    gapDeg: number;
}

export interface LevelPreview {
    rows: number;
    cols: number;
    road: [number, number][];
    fruits: [number, number, string][];
    island?: [number, number][];
    /** 曲线关卡：采样中心线（x = col，y = row）。 */
    polyline?: [number, number][];
    /** 预留：曲线水果区域后方的半扇形环地面。 */
    ring?: RingPreview;
}

export interface LevelSummary {
    id: string;
    order: number;
    name: string;
    subtitle: string;
    tag: string;
    difficulty: number;
    accent: string;
    file: string;
    preview: LevelPreview;
}

export interface LevelsResponse {
    version: number;
    current: { id: string } | null;
    levels: LevelSummary[];
}

export interface SelectResult {
    ok: boolean;
    id?: string;
    levelEpoch?: number;
    seed?: number;
    error?: string;
    detail?: string;
}

/**
 * 快照轮询器 + 三个关卡目录接口。控制器从不预测
 * 状态：它只绘制最近收到的快照，并把玩家意图
 * 提交回 host。
 */
export class GameBridge {
    snapshot: Snapshot | null = null;
    lastError = '';
    connected = false;
    lastEventSeq = 0;
    private running = false;
    private timer: ReturnType<typeof setTimeout> | null = null;
    private offline = false;
    private everConnected = false;

    constructor(private readonly baseUrl: string,
                private readonly intervalMs: number,
                private readonly onSnapshot: (snapshot: Snapshot) => void,
                private readonly onStatus: (connected: boolean, detail: string) => void = () => undefined) {}

    start(): void {
        if (this.running) {
            return;
        }
        this.running = true;
        this.tick();
    }

    stop(): void {
        this.running = false;
        if (this.timer !== null) {
            clearTimeout(this.timer);
            this.timer = null;
        }
    }

    private schedule(): void {
        if (!this.running) {
            return;
        }
        this.timer = setTimeout(() => this.tick(), this.intervalMs);
    }

    private tick(): void {
        this.poll().catch(() => undefined).then(() => this.schedule());
    }

    private async poll(): Promise<void> {
        try {
            const response = await fetch(`${this.baseUrl}/state?since=${this.lastEventSeq}`, { cache: 'no-store' });
            if (!response.ok) {
                this.lastError = `HTTP ${response.status}`;
                this.goOffline(this.lastError);
                return;
            }
            const snapshot = (await response.json()) as Snapshot;
            if (!this.everConnected) {
                this.everConnected = true;
                this.connected = true;
                this.onStatus(true, '');
            }
            if (this.offline) {
                // host 重启后序列号会从头开始，因此
                // 客户端重新同步，而不是忽略每一个快照。
                this.offline = false;
                this.connected = true;
                this.lastEventSeq = 0;
                this.snapshot = null;
                this.onStatus(true, '');
            }
            if (snapshot.seq <= (this.snapshot ? this.snapshot.seq : -1)) {
                return;
            }
            this.snapshot = snapshot;
            this.lastEventSeq = snapshot.eventSeq;
            this.lastError = '';
            this.onSnapshot(snapshot);
        } catch (error) {
            this.goOffline(error instanceof Error ? error.message : 'network error');
        }
    }

    private goOffline(reason: string): void {
        const wasOnline = !this.offline;
        this.offline = true;
        this.connected = false;
        this.lastError = reason;
        if (wasOnline) {
            this.onStatus(false, reason);
        }
    }

    /** GET /levels：主页面唯一的数据来源。 */
    async fetchLevels(): Promise<LevelsResponse> {
        const response = await fetch(`${this.baseUrl}/levels`, { cache: 'no-store' });
        if (!response.ok) {
            throw new Error(`HTTP ${response.status}`);
        }
        return (await response.json()) as LevelsResponse;
    }

    /** POST /level/select：由服务端权威决定关卡切换。 */
    async selectLevel(id: string): Promise<SelectResult> {
        const response = await fetch(`${this.baseUrl}/level/select`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id }),
        });
        const body = (await response.json()) as SelectResult;
        if (!response.ok && body.error === undefined) {
            body.ok = false;
            body.error = `HTTP ${response.status}`;
        }
        return body;
    }

    /** POST /pause：主页面可见时冻结 host 的 tick。 */
    async setPaused(paused: boolean): Promise<boolean> {
        const response = await fetch(`${this.baseUrl}/pause`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ paused }),
        });
        if (!response.ok) {
            return false;
        }
        const body = (await response.json()) as { ok: boolean; paused: boolean };
        return body.ok === true;
    }

    /** 提交玩家命令；由 host 判定是否合法。 */
    async submit(type: 'select_lane' | 'select_slot', index: number): Promise<CommandReceipt> {
        const body = type === 'select_lane' ? { type, lane: index } : { type, slot: index };
        const response = await fetch(`${this.baseUrl}/command`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        return (await response.json()) as CommandReceipt;
    }

    /**
     * 预留的技能命令（任务书 5.8）。当前阶段始终返回
     * ITEM_NOT_IMPLEMENTED；UI 会显示回执中的原因。
     */
    async useItem(itemId: string, targetCar?: string, center?: [number, number],
                  radius?: number): Promise<CommandReceipt> {
        const body: Record<string, unknown> = { type: 'use_item', itemId };
        if (targetCar) {
            body.targetCar = targetCar;
        }
        if (center) {
            body.center = center;
        }
        if (typeof radius === 'number') {
            body.radius = radius;
        }
        const response = await fetch(`${this.baseUrl}/command`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
        return (await response.json()) as CommandReceipt;
    }

    async reset(seed: number): Promise<void> {
        await fetch(`${this.baseUrl}/reset`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ seed }),
        });
    }

    /** 将客户端诊断信息（node 数量）转发到 host 日志。 */
    async report(payload: unknown): Promise<void> {
        try {
            await fetch(`${this.baseUrl}/client_report`, {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(payload),
            });
        } catch (error) {
            // 诊断上报尽力而为，且绝不影响玩法。
            console.log('[FC_REPORT_FAILED]' + (error instanceof Error ? error.message : String(error)));
        }
    }
}
