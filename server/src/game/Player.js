class Player {
    constructor(id, name) {
        this.id = id;
        this.name = name;
        this.transform = {
            position: { x: 0, y: 30, z: 8 },
            rotation: { x: 0, y: 0, z: 0 },
            scale: { x: 1, y: 1, z: 1 }
        };
        this.velocity = { x: 0, y: 0, z: 0 };
        this.speed = 10.0;
        this.sensitivity = 0.1;
        this.yaw = -90;
        this.pitch = 0;
        this.grounded = false;
        this.height = 1.8;
        this.heightAt = null; // function(x, z) -> terrain height, set by world
    }

    updateFromInput(input, dt) {
        // Calculate movement direction
        const yawRad = this.yaw * Math.PI / 180;
        const pitchRad = this.pitch * Math.PI / 180;

        const forward = {
            x: Math.cos(pitchRad) * Math.cos(yawRad),
            y: Math.sin(pitchRad),
            z: Math.cos(pitchRad) * Math.sin(yawRad)
        };

        const right = {
            x: Math.cos(yawRad - Math.PI / 2),
            y: 0,
            z: Math.sin(yawRad - Math.PI / 2)
        };

        // Project forward onto horizontal plane for ground movement
        let fx = forward.x, fz = forward.z;
        const flen = Math.sqrt(fx*fx + fz*fz) || 1;
        fx /= flen; fz /= flen;

        const rightLen = Math.sqrt(right.x**2 + right.y**2 + right.z**2) || 1;
        right.x /= rightLen; right.y /= rightLen; right.z /= rightLen;

        // Apply input movement
        const moveSpeed = this.speed;
        let wishX = (fx * (input.forward || 0) + right.x * (input.right || 0)) * moveSpeed;
        let wishZ = (fz * (input.forward || 0) + right.z * (input.right || 0)) * moveSpeed;
        const wishLen = Math.sqrt(wishX*wishX + wishZ*wishZ);
        if (wishLen > moveSpeed) {
            wishX = wishX / wishLen * moveSpeed;
            wishZ = wishZ / wishLen * moveSpeed;
        }
        this.velocity.x = wishX;
        this.velocity.z = wishZ;

        // Jump
        if ((input.jump || 0) > 0 && this.grounded) {
            this.velocity.y = 9.0;
            this.grounded = false;
        }
    }

    physicsStep(dt, gravity) {
        this.velocity.y += gravity * dt;

        this.transform.position.x += this.velocity.x * dt;
        this.transform.position.y += this.velocity.y * dt;
        this.transform.position.z += this.velocity.z * dt;

        // Ground collision with terrain heightmap
        let groundY = 0.0;
        if (this.heightAt) {
            groundY = this.heightAt(this.transform.position.x, this.transform.position.z) + this.height;
        } else {
            groundY = this.height;
        }

        if (this.transform.position.y <= groundY) {
            this.transform.position.y = groundY;
            this.velocity.y = 0;
            this.grounded = true;
        } else {
            this.grounded = false;
        }
    }

    getState() {
        return {
            id: this.id,
            name: this.name,
            position: {
                x: Math.round(this.transform.position.x * 10) / 10,
                y: Math.round(this.transform.position.y * 10) / 10,
                z: Math.round(this.transform.position.z * 10) / 10
            },
            rotation: { yaw: this.yaw, pitch: this.pitch }
        };
    }
}

module.exports = Player;
