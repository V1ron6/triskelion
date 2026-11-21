import { S3Client, GetObjectCommand, PutObjectCommand } from '@aws-sdk/client-s3';
import { getSignedUrl } from '@aws-sdk/s3-request-presigner';

// Cloudflare R2 through its S3-compatible API. Artifacts never pass through this server.
export function createStorage({ accountId, accessKeyId, secretAccessKey, bucket }) {
  const s3 = new S3Client({
    region: 'auto',
    endpoint: `https://${accountId}.r2.cloudflarestorage.com`,
    credentials: { accessKeyId, secretAccessKey },
  });
  return {
    presignPut: (key, expiresIn = 900) =>
      getSignedUrl(s3, new PutObjectCommand({ Bucket: bucket, Key: key }), { expiresIn }),
    presignGet: (key, expiresIn = 900) =>
      getSignedUrl(s3, new GetObjectCommand({ Bucket: bucket, Key: key }), { expiresIn }),
  };
}
