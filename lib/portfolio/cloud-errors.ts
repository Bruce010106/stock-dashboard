export type PortfolioCloudErrorStatus = 400 | 401 | 502 | 503;
export type PortfolioCloudErrorCode = 'VALIDATION' | 'AUTH_REQUIRED' | 'NOT_CONFIGURED' | 'STORAGE';

export class PortfolioCloudError extends Error {
  readonly status: PortfolioCloudErrorStatus;
  readonly code: PortfolioCloudErrorCode;

  constructor(status: PortfolioCloudErrorStatus, code: PortfolioCloudErrorCode, message: string) {
    super(message);
    this.status = status;
    this.code = code;
    this.name = 'PortfolioCloudError';
  }
}
