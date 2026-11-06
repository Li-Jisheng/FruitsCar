import { GameBridge } from '../bridge/GameBridge';

/** 将 UI 点击转换为协议命令。它从不判定合法性。 */
export class InputController {
    constructor(private readonly bridge: GameBridge,
                private readonly onFeedback: (message: string, accepted: boolean) => void) {}

    async click(kind: 'lane' | 'slot', index: number): Promise<void> {
        const receipt = await this.bridge.submit(kind === 'lane' ? 'select_lane' : 'select_slot', index);
        if (!receipt.accepted) {
            this.onFeedback(this.describe(receipt.reason), false);
            return;
        }
        this.onFeedback('', true);
    }

    private describe(reason: string): string {
        switch (reason) {
            case 'COOLDOWN':
                return '冷却中';
            case 'ROAD_BLOCKED':
                return '入口拥堵';
            case 'LANE_EMPTY':
                return '该车道已空';
            case 'SLOT_EMPTY':
                return '该车位为空';
            case 'EXPECT_SLOT_SELECT':
                return '必须从停车位选车';
            case 'BUSY':
                return '本帧已选过';
            case 'GAME_OVER':
                return '本局已结束';
            default:
                return `拒绝: ${reason}`;
        }
    }
}
