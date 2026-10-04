#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <stdarg.h>
#include <stdio.h>

#include "physics.hpp"
#include "gltf.hpp"
#include "memory.hpp"
#include "log.hpp"

namespace physics {

	namespace layers {
		static constexpr JPH::ObjectLayer NON_MOVING = 0;
		static constexpr JPH::ObjectLayer MOVING     = 1;
	}

	namespace bp_layers {
		static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
		static constexpr JPH::BroadPhaseLayer MOVING(1);
		static constexpr JPH::uint COUNT = 2;
	}

	class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
	public:
		JPH::uint GetNumBroadPhaseLayers() const override { return bp_layers::COUNT; }
		JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
			return layer == layers::NON_MOVING ? bp_layers::NON_MOVING : bp_layers::MOVING;
		}
	};

	class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
			return layer == layers::MOVING || bp == bp_layers::MOVING;
		}
	};

	class ObjectPairs final : public JPH::ObjectLayerPairFilter {
	public:
		bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
			return a == layers::MOVING || b == layers::MOVING;
		}
	};

	static constexpr JPH::uint MAX_BODIES              = 4096;
	static constexpr JPH::uint MAX_BODY_PAIRS          = 4096;
	static constexpr JPH::uint MAX_CONTACT_CONSTRAINTS = 2048;

	static BroadPhaseLayers   bp_interface;
	static ObjectVsBroadPhase obj_vs_bp_filter;
	static ObjectPairs        obj_pair_filter;

	// heap-allocated: Jolt's operator new needs RegisterDefaultAllocator first
	static JPH::TempAllocatorImpl*    temp_allocator;
	static JPH::JobSystemThreadPool*  job_system;
	static JPH::PhysicsSystem*        phys_system;

	static constexpr u32 MAX_CHARACTERS = 4;
	static JPH::Ref<JPH::CharacterVirtual> characters[MAX_CHARACTERS];

	static void trace(const char* fmt, ...) {
		char buf[1024];
		va_list args;
		va_start(args, fmt);
		vsnprintf(buf, sizeof(buf), fmt, args);
		va_end(args);
		logger::warn("jolt: %s", buf);
	}

	bool init() {
		JPH::RegisterDefaultAllocator();
		JPH::Trace = trace;
		JPH::Factory::sInstance = new JPH::Factory();
		JPH::RegisterTypes();

		temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
		job_system = new JPH::JobSystemThreadPool(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers);
		phys_system = new JPH::PhysicsSystem();
		phys_system->Init(MAX_BODIES, 0, MAX_BODY_PAIRS, MAX_CONTACT_CONSTRAINTS,
			bp_interface, obj_vs_bp_filter, obj_pair_filter);
		return true;
	}

	void shutdown() {
		for (u32 i = 0; i < MAX_CHARACTERS; i++) characters[i] = nullptr;
		delete phys_system;
		delete job_system;
		delete temp_allocator;
		phys_system = nullptr;
		job_system = nullptr;
		temp_allocator = nullptr;

		JPH::UnregisterTypes();
		delete JPH::Factory::sInstance;
		JPH::Factory::sInstance = nullptr;
	}

	void step(f32 dt) {
		phys_system->Update(dt, 1, temp_allocator, job_system);
	}

	static JPH::Vec3 to_jph(vec3 v) { return JPH::Vec3(v.x, v.y, v.z); }
	static JPH::Quat to_jph(quat q) { return JPH::Quat(q.x, q.y, q.z, q.w); }
	static vec3 from_jph(JPH::Vec3Arg v) { return { v.GetX(), v.GetY(), v.GetZ() }; }
	static quat from_jph(JPH::QuatArg q) { return { q.GetX(), q.GetY(), q.GetZ(), q.GetW() }; }

	static JPH::ShapeSettings::ShapeResult offset_shape(const JPH::ShapeSettings::ShapeResult& inner, vec3 center) {
		if (inner.HasError() || length_sq(center) < 1e-8f) return inner;
		return JPH::RotatedTranslatedShapeSettings(to_jph(center), JPH::Quat::sIdentity(), inner.Get()).Create();
	}

	static JPH::ShapeSettings::ShapeResult build_shape(const ShapeDesc& d, bool dynamic) {
		ShapeType type = d.type;
		if (type == ShapeType::Mesh && dynamic) {
			logger::error("physics: mesh shape can't be dynamic, using hull");
			type = ShapeType::Hull;
		}

		switch (type) {
		case ShapeType::Box: {
			// convex radius can't exceed the thinnest half extent
			f32 thinnest = d.half_extents.x < d.half_extents.y ? d.half_extents.x : d.half_extents.y;
			if (d.half_extents.z < thinnest) thinnest = d.half_extents.z;
			f32 convex_radius = thinnest < JPH::cDefaultConvexRadius ? thinnest : JPH::cDefaultConvexRadius;
			return offset_shape(JPH::BoxShapeSettings(to_jph(d.half_extents), convex_radius).Create(), d.center);
		}
		case ShapeType::Sphere:
			return offset_shape(JPH::SphereShapeSettings(d.radius).Create(), d.center);
		case ShapeType::Hull: {
			JPH::Array<JPH::Vec3> points;
			points.reserve(d.point_count);
			for (u32 i = 0; i < d.point_count; i++) points.push_back(to_jph(d.points[i]));
			return JPH::ConvexHullShapeSettings(points).Create();
		}
		case ShapeType::Mesh: {
			JPH::VertexList verts;
			verts.reserve(d.point_count);
			for (u32 i = 0; i < d.point_count; i++) verts.push_back(JPH::Float3(d.points[i].x, d.points[i].y, d.points[i].z));
			JPH::IndexedTriangleList tris;
			tris.reserve(d.index_count / 3);
			for (u32 i = 0; i + 2 < d.index_count; i += 3) tris.push_back(JPH::IndexedTriangle(d.indices[i], d.indices[i + 1], d.indices[i + 2], 0));
			return JPH::MeshShapeSettings(std::move(verts), std::move(tris)).Create();
		}
		}
		JPH::ShapeSettings::ShapeResult bad;
		bad.SetError("unknown shape type");
		return bad;
	}

	BodyHandle create_body(const ShapeDesc& shape, vec3 pos, quat rot, bool dynamic, f32 mass) {
		JPH::ShapeSettings::ShapeResult result = build_shape(shape, dynamic);
		if (result.HasError()) {
			logger::error("physics: shape creation failed: %s", result.GetError().c_str());
			return INVALID_BODY;
		}

		JPH::BodyCreationSettings settings(result.Get(), to_jph(pos), to_jph(rot),
			dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
			dynamic ? layers::MOVING : layers::NON_MOVING);
		if (dynamic) {
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = mass;
		}

		JPH::BodyID id = phys_system->GetBodyInterface().CreateAndAddBody(settings,
			dynamic ? JPH::EActivation::Activate : JPH::EActivation::DontActivate);
		if (id.IsInvalid()) {
			logger::error("physics: body limit reached (%u)", MAX_BODIES);
			return INVALID_BODY;
		}
		return id.GetIndexAndSequenceNumber();
	}

	void destroy_body(BodyHandle body) {
		if (body == INVALID_BODY) return;
		JPH::BodyInterface& bi = phys_system->GetBodyInterface();
		bi.RemoveBody(JPH::BodyID(body));
		bi.DestroyBody(JPH::BodyID(body));
	}

	void get_transform(BodyHandle body, vec3* pos, quat* rot) {
		JPH::RVec3 p;
		JPH::Quat r;
		phys_system->GetBodyInterface().GetPositionAndRotation(JPH::BodyID(body), p, r);
		*pos = from_jph(p);
		*rot = from_jph(r);
	}

	void set_transform(BodyHandle body, vec3 pos, quat rot) {
		phys_system->GetBodyInterface().SetPositionAndRotation(JPH::BodyID(body),
			to_jph(pos), to_jph(rot), JPH::EActivation::Activate);
	}

	u32 create_bodies(const renderer::GltfModel& model, const mat4& transform, ObjectBody* out, u32 max) {
		u32 count = 0;
		for (u32 o = 0; o < model.object_count; o++) {
			const renderer::GltfObject& obj = model.objects[o];
			if (obj.physics.type == renderer::PhysicsType::None) continue;
			if (count >= max) {
				logger::error("physics: create_bodies out of space (%u)", max);
				break;
			}
			bool dynamic = obj.physics.type == renderer::PhysicsType::Dynamic;

			vec3 pos, scale;
			quat rot;
			mat4_decompose(transform * obj.world_transform, &pos, &rot, &scale);

			// gather every primitive of this object, scaled into body space
			u32 vertex_count = 0, index_count = 0;
			for (u32 n = 0; n < model.node_count; n++) {
				if (model.nodes[n].object_index != o) continue;
				const renderer::MeshData& m = model.primitives[model.nodes[n].primitive_index].mesh;
				vertex_count += m.vertex_count;
				index_count  += m.index_count;
			}
			if (vertex_count == 0) continue;

			vec3* points  = (vec3*)memory::malloc(sizeof(vec3) * vertex_count);
			u32*  indices = (u32*)memory::malloc(sizeof(u32) * index_count);
			vec3 lo = {  1e30f,  1e30f,  1e30f };
			vec3 hi = { -1e30f, -1e30f, -1e30f };
			u32 vbase = 0, ibase = 0;
			for (u32 n = 0; n < model.node_count; n++) {
				if (model.nodes[n].object_index != o) continue;
				const renderer::MeshData& m = model.primitives[model.nodes[n].primitive_index].mesh;
				for (u32 v = 0; v < m.vertex_count; v++) {
					vec3 p = { m.positions[v].x * scale.x, m.positions[v].y * scale.y, m.positions[v].z * scale.z };
					points[vbase + v] = p;
					if (p.x < lo.x) lo.x = p.x; if (p.x > hi.x) hi.x = p.x;
					if (p.y < lo.y) lo.y = p.y; if (p.y > hi.y) hi.y = p.y;
					if (p.z < lo.z) lo.z = p.z; if (p.z > hi.z) hi.z = p.z;
				}
				for (u32 i = 0; i < m.index_count; i++) indices[ibase + i] = vbase + m.indices[i];
				vbase += m.vertex_count;
				ibase += m.index_count;
			}

			ShapeDesc shape = {};
			shape.half_extents = (hi - lo) * 0.5f;
			shape.center       = (hi + lo) * 0.5f;
			shape.radius       = shape.half_extents.x;
			if (shape.half_extents.y > shape.radius) shape.radius = shape.half_extents.y;
			if (shape.half_extents.z > shape.radius) shape.radius = shape.half_extents.z;
			shape.points      = points;
			shape.point_count = vertex_count;
			shape.indices     = indices;
			shape.index_count = index_count;
			switch (obj.physics.shape) {
			case renderer::PhysicsShape::Auto:   shape.type = dynamic ? ShapeType::Hull : ShapeType::Mesh; break;
			case renderer::PhysicsShape::Box:    shape.type = ShapeType::Box;    break;
			case renderer::PhysicsShape::Sphere: shape.type = ShapeType::Sphere; break;
			case renderer::PhysicsShape::Hull:   shape.type = ShapeType::Hull;   break;
			case renderer::PhysicsShape::Mesh:   shape.type = ShapeType::Mesh;   break;
			}

			BodyHandle body = create_body(shape, pos, rot, dynamic, obj.physics.mass);
			memory::free(points);
			memory::free(indices);
			if (body != INVALID_BODY) out[count++] = { o, body, dynamic, scale };
		}

		phys_system->OptimizeBroadPhase();
		return count;
	}

	bool raycast(vec3 from, vec3 dir, f32 length, RayHit* out) {
		JPH::RRayCast ray(to_jph(from), to_jph(dir) * length);
		JPH::RayCastResult hit;
		if (!phys_system->GetNarrowPhaseQuery().CastRay(ray, hit)) return false;

		JPH::RVec3 point = ray.GetPointOnRay(hit.mFraction);
		JPH::Vec3 normal = -to_jph(dir);
		JPH::BodyLockRead lock(phys_system->GetBodyLockInterface(), hit.mBodyID);
		if (lock.Succeeded()) normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);

		out->position = from_jph(point);
		out->normal   = from_jph(normal);
		out->distance = hit.mFraction * length;
		out->body     = hit.mBodyID.GetIndexAndSequenceNumber();
		return true;
	}

	static JPH::CharacterVirtual* get_character(CharacterHandle character) {
		return character < MAX_CHARACTERS ? characters[character].GetPtr() : nullptr;
	}

	CharacterHandle create_character(vec3 feet, f32 radius, f32 height) {
		u32 slot = 0;
		while (slot < MAX_CHARACTERS && characters[slot] != nullptr) slot++;
		if (slot == MAX_CHARACTERS) {
			logger::error("physics: character limit reached (%u)", MAX_CHARACTERS);
			return INVALID_CHARACTER;
		}

		// capsule shifted up so the character position sits at the feet
		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape = JPH::RotatedTranslatedShapeSettings(JPH::Vec3(0.0f, 0.5f * height, 0.0f), JPH::Quat::sIdentity(),
			new JPH::CapsuleShape(0.5f * height - radius, radius)).Create().Get();
		// only contacts in the bottom hemisphere count as ground
		settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

		characters[slot] = new JPH::CharacterVirtual(settings, to_jph(feet), JPH::Quat::sIdentity(), phys_system);
		return slot;
	}

	void destroy_character(CharacterHandle character) {
		if (character < MAX_CHARACTERS) characters[character] = nullptr;
	}

	vec3 move_character(CharacterHandle character, vec3 velocity, f32 dt) {
		JPH::CharacterVirtual* ch = get_character(character);
		if (!ch) return velocity;
		ch->SetLinearVelocity(to_jph(velocity));
		JPH::CharacterVirtual::ExtendedUpdateSettings settings;
		ch->ExtendedUpdate(dt, phys_system->GetGravity(), settings,
			phys_system->GetDefaultBroadPhaseLayerFilter(layers::MOVING),
			phys_system->GetDefaultLayerFilter(layers::MOVING),
			JPH::BodyFilter(), JPH::ShapeFilter(), *temp_allocator);

		// Jolt doesn't write the slid velocity back; clip against walls/ceilings (floors left to the caller)
		JPH::Vec3 v = to_jph(velocity);
		for (const JPH::CharacterVirtual::Contact& c : ch->GetActiveContacts()) {
			if (!c.mHadCollision || c.mWasDiscarded || !ch->IsSlopeTooSteep(c.mContactNormal)) continue;
			f32 into = v.Dot(c.mContactNormal);
			if (into < 0.0f) v -= c.mContactNormal * into;
		}
		return from_jph(v);
	}

	CharacterState character_state(CharacterHandle character) {
		const JPH::CharacterVirtual* ch = get_character(character);
		if (!ch) return {};
		return { from_jph(ch->GetPosition()), ch->GetGroundState() == JPH::CharacterVirtual::EGroundState::OnGround };
	}

	void set_character_position(CharacterHandle character, vec3 feet) {
		if (JPH::CharacterVirtual* ch = get_character(character)) ch->SetPosition(to_jph(feet));
	}

}
