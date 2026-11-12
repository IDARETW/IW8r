import io, sys
root = sys.argv[1]


def patch(rel, pairs):
    p = root + '/' + rel
    s = io.open(p, encoding='utf-8', newline='').read()
    nl = '\r\n' if '\r\n' in s else '\n'
    for a, b in pairs:
        a = a.replace('\n', nl); b = b.replace('\n', nl)
        assert s.count(a) == 1, (rel, s.count(a), a[:70])
        s = s.replace(a, b)
    io.open(p, 'w', encoding='utf-8', newline='').write(s)


patch('src/convert/iw3_iw8/convert.cpp', [
 ('''			scriptable_records.assign(block.mapInstances, block.mapInstances + before);
			std::size_t moved = 0, added = 0;
			if (node.contains("move"))
			{
				std::size_t at = 0;
				for (const auto& entry : node.at("move"))
				{
					const auto where = "scriptable_instances.move[" + std::to_string(at++) + "]";
					auto& record = scriptable_records[index_of(entry, "index", where)];
					placement(entry, where, record);
					++moved;
				}
			}''', '''			scriptable_records.assign(block.mapInstances, block.mapInstances + before);
			std::size_t moved = 0, added = 0, moved_with_dynamic = 0, relinked = 0;
			// A move of an instance whose dynamic entities are ordinary ones (not sparse occluders such as doors, not
			// transient): the entities copy the placement (initialPose origin = the instance origin, quat = AnglesToQuat
			// of its angles) and sit in the spatial partition the MapEnts tree gives for the origin, chained per partition
			// in DynEntityList.dynEntSpatialPopulation in descending entry order (IW8 Radiant research 89 section 8: exact
			// on Quarry, tools/dynent_pose_rules.py). Entries of the no-spatial list (partition 0) keep it.
			const auto move_dynamic = [&](const std::uint32_t index, const ScriptableInstance& record, const std::string& where)
			{
				auto* map_ents = ents;
				const auto quat_of = [](const vec3_t& a, float* q)
				{
					const auto rad = 0.00872664625997164788; // pi / 360: half of the angle in radians per degree
					const double cp = std::cos(a.v[0] * rad), sp = std::sin(a.v[0] * rad), cy = std::cos(a.v[1] * rad),
						sy = std::sin(a.v[1] * rad), cr = std::cos(a.v[2] * rad), sr = std::sin(a.v[2] * rad);
					q[0] = static_cast<float>(sr * cp * cy - cr * sp * sy);
					q[1] = static_cast<float>(cr * sp * cy + sr * cp * sy);
					q[2] = static_cast<float>(cr * cp * sy - sr * sp * cy);
					q[3] = static_cast<float>(cr * cp * cy + sr * sp * sy);
				};
				for (auto& asset : zone->assets)
				{
					if (asset.type != ASSET_TYPE_DYNENTITYLIST || asset.referenced || !asset.header)
					{
						continue;
					}
					auto* list = static_cast<DynEntityList*>(asset.header);
					for (auto side = 0; side < 2; side++)
					{
						auto* defs = list->dynEntDefList[side];
						for (std::uint32_t i = 0; defs && i < list->dynEntCount[side]; i++)
						{
							auto& def = defs[i];
							if ((def.type != DYNENT_TYPE_SCRIPTABLEINST && def.type != DYNENT_TYPE_SCRIPTABLEPHYSICS) || def.scriptableMapIndex != index)
							{
								continue;
							}
							if (def.priority == DYNENT_PRIORITY_SPARSE_OCCLUDER || def.isTransient)
							{
								throw std::runtime_error(where + ": instance " + std::to_string(index) +
									" is a door or another sparse occluder (or streamed): its dynamic entity cannot be moved yet");
							}
							def.initialPose.origin = record.initialOrigin;
							quat_of(record.initialAngles, def.initialPose.quat.v);
							if (def.spatialPartitionIndex == 0)
							{
								continue; // the no-spatial list
							}
							auto* population = map_ents->dynEntSpatialPopulation[side];
							auto* chains = list->dynEntSpatialPopulation[side];
							if (!population || !population->tree || !population->tree->partitions || !chains || !chains->buckets)
							{
								throw std::runtime_error(where + ": the map has no spatial tree for the dynamic entity of instance " + std::to_string(index));
							}
							const auto* tree = population->tree;
							std::uint32_t node = 0;
							for (std::uint32_t guard = 0; guard < tree->partitionCount; guard++)
							{
								const auto& part = tree->partitions[node];
								if (part.axis == 3)
								{
									break;
								}
								node = record.initialOrigin.v[part.axis] < part.dist ? node + 1 : part.rightChild;
								if (node >= tree->partitionCount)
								{
									throw std::runtime_error(where + ": the spatial tree is not walkable");
								}
							}
							if (node == def.spatialPartitionIndex)
							{
								continue;
							}
							const auto bucket_of = [&](const std::uint32_t partition) -> std::int64_t
							{
								for (std::uint32_t b = 0; b < chains->partitionCount; b++)
								{
									if (chains->partitionIds[b] == partition)
									{
										return b;
									}
								}
								return -1;
							};
							const auto from = bucket_of(def.spatialPartitionIndex), to = bucket_of(node);
							if (from < 0 || to < 0)
							{
								throw std::runtime_error(where + ": instance " + std::to_string(index) +
									" would move into an area of the map that holds no other object of this kind (spatial partition " +
									std::to_string(node) + "); move it less far");
							}
							constexpr std::uint32_t none = 0xFFFFFFFF;
							// Unlink from the old chain.
							{
								auto& head = chains->buckets[from].spatialNodeNext;
								std::uint32_t prev = none;
								for (std::uint32_t at = head, guard = 0; at != none && guard < list->dynEntCount[side]; at = defs[at].spatialNode.spatialNodeNext, guard++)
								{
									if (at == i)
									{
										if (prev == none) head = def.spatialNode.spatialNodeNext;
										else defs[prev].spatialNode.spatialNodeNext = def.spatialNode.spatialNodeNext;
										if (chains->bucketTails && chains->bucketTails[from].spatialNodeNext == i)
										{
											chains->bucketTails[from].spatialNodeNext = prev;
										}
										break;
									}
									prev = at;
								}
							}
							// Link into the new chain, keeping the descending order.
							{
								auto& head = chains->buckets[to].spatialNodeNext;
								std::uint32_t prev = none, at = head;
								for (std::uint32_t guard = 0; at != none && at > i && guard < list->dynEntCount[side]; guard++)
								{
									prev = at;
									at = defs[at].spatialNode.spatialNodeNext;
								}
								def.spatialNode.spatialNodeNext = at;
								if (prev == none) head = i;
								else defs[prev].spatialNode.spatialNodeNext = i;
								if (chains->bucketTails && at == none)
								{
									chains->bucketTails[to].spatialNodeNext = i;
								}
							}
							def.spatialPartitionIndex = node;
							++relinked;
						}
					}
				}
			};
			if (node.contains("move"))
			{
				std::size_t at = 0;
				for (const auto& entry : node.at("move"))
				{
					const auto where = "scriptable_instances.move[" + std::to_string(at++) + "]";
					if (!entry.is_object() || !entry.contains("index") || !entry.at("index").is_number_unsigned() ||
						entry.at("index").get<std::uint64_t>() >= before)
					{
						throw std::runtime_error(where + ": no \\"index\\" of a placed instance (0 .. " + std::to_string(before) + ")");
					}
					const auto index = static_cast<std::uint32_t>(entry.at("index").get<std::uint64_t>());
					auto& record = scriptable_records[index];
					placement(entry, where, record);
					if (with_dynamic_entity.contains(index))
					{
						move_dynamic(index, record, where);
						++moved_with_dynamic;
					}
					++moved;
				}
			}'''),
 ('''			report["scriptable_instances_added"] = added;''', '''			report["scriptable_instances_added"] = added;
			report["scriptable_instances_moved_with_dynamic_entity"] = moved_with_dynamic;
			report["scriptable_dynamic_entities_relinked"] = relinked;'''),
])
print('ok')
