class Physics {
    static GRAVITY = -20.0;

    static checkAABBCollision(a, b) {
        return (
            a.position.x - a.scale.x/2 < b.position.x + b.scale.x/2 &&
            a.position.x + a.scale.x/2 > b.position.x - b.scale.x/2 &&
            a.position.y - a.scale.y/2 < b.position.y + b.scale.y/2 &&
            a.position.y + a.scale.y/2 > b.position.y - b.scale.y/2 &&
            a.position.z - a.scale.z/2 < b.position.z + b.scale.z/2 &&
            a.position.z + a.scale.z/2 > b.position.z - b.scale.z/2
        );
    }

    static resolveCollision(entity, staticEntity) {
        // Simple AABB push-out
        const dx = entity.position.x - staticEntity.position.x;
        const dy = entity.position.y - staticEntity.position.y;
        const dz = entity.position.z - staticEntity.position.z;

        const overlapX = (entity.scale.x + staticEntity.scale.x) / 2 - Math.abs(dx);
        const overlapY = (entity.scale.y + staticEntity.scale.y) / 2 - Math.abs(dy);
        const overlapZ = (entity.scale.z + staticEntity.scale.z) / 2 - Math.abs(dz);

        if (overlapX > 0 && overlapY > 0 && overlapZ > 0) {
            if (overlapX < overlapY && overlapX < overlapZ) {
                entity.position.x += (dx > 0 ? overlapX : -overlapX);
            } else if (overlapY < overlapX && overlapY < overlapZ) {
                entity.position.y += (dy > 0 ? overlapY : -overlapY);
            } else {
                entity.position.z += (dz > 0 ? overlapZ : -overlapZ);
            }
        }
    }
}

module.exports = Physics;
