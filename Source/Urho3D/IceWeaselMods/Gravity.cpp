//
// Copyright (c) 2008-2016 the Urho3D project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "../Core/Context.h"
#include "../IceWeaselMods/Gravity.h"
#include "../IceWeaselMods/GravityVector.h"
#include "../IceWeaselMods/IceWeasel.h"
#include "../IceWeaselMods/TetrahedralMesh.h"
#include "../Scene/SceneEvents.h"
#include "../Scene/Node.h"

namespace Urho3D
{


// ----------------------------------------------------------------------------
Gravity::Gravity(Context* context)
    : Component(context),
    gravity_(9.81f)
{
    SubscribeToEvent(E_COMPONENTADDED, URHO3D_HANDLER(Gravity, HandleComponentAdded));
    SubscribeToEvent(E_COMPONENTREMOVED, URHO3D_HANDLER(Gravity, HandleComponentRemoved));
    SubscribeToEvent(E_NODEADDED, URHO3D_HANDLER(Gravity, HandleNodeAdded));
    SubscribeToEvent(E_NODEREMOVED, URHO3D_HANDLER(Gravity, HandleNodeRemoved));
}

// ----------------------------------------------------------------------------
Gravity::~Gravity()
{
}

// ----------------------------------------------------------------------------
void Gravity::RegisterObject(Context* context)
{
    context->RegisterFactory<Gravity>(ICEWEASELMODS_CATEGORY);

    URHO3D_ACCESSOR_ATTRIBUTE("Global Gravity", GetGlobalGravity, SetGlobalGravity, float, 9.81, AM_DEFAULT);
}

// ----------------------------------------------------------------------------
Vector3 Gravity::QueryGravity(Vector3 worldLocation)
{
    /*
    // TODO Really shitty method of finding closest node
    float distance = std::numeric_limits<float>::max();
    PODVector<GravityVector*>::ConstIterator it = gravityVectors_.Begin();
    GravityVector* foundGravityVector = NULL;
    for(; it != gravityVectors_.End(); ++it)
    {
        float newDistance = (worldLocation - (*it)->GetPosition()).Length();
        if(newDistance < distance)
        {
            foundGravityVector = *it;
            distance = newDistance;
        }
    }

    // No node was found? No gravity nodes exist. Provide default vector
    if(foundGravityVector == NULL)
        return Vector3::DOWN * gravity_;

    return foundGravityVector->GetDirection() * foundGravityVector->GetForceFactor() * gravity_;*/

    // No node was found? No gravity nodes exist. Provide default vector
    if(!tetrahedralMesh_)
        return Vector3::DOWN * gravity_;

    Vector4 bary;
    Tetrahedron* tetrahedron = tetrahedralMesh_->Query(worldLocation);
    if(!tetrahedron)
        return Vector3::DOWN * gravity_;
}

// ----------------------------------------------------------------------------
void Gravity::DrawDebugGeometry(DebugRenderer* debug, bool depthTest)
{
    PODVector<GravityVector*>::ConstIterator it = gravityVectors_.Begin();
    for(; it != gravityVectors_.End(); ++it)
        (*it)->DrawDebugGeometry(debug, depthTest);
}

// ----------------------------------------------------------------------------
void Gravity::RebuildTetrahedralMesh()
{
    Vector<Vector3> pointList;
    PODVector<GravityVector*>::ConstIterator gravityVector = gravityVectors_.Begin();
    pointList.Reserve(gravityVectors_.Size());
    for(; gravityVector != gravityVectors_.End(); ++gravityVector)
        pointList.Push((*gravityVector)->GetPosition());

    tetrahedralMesh_->Build(pointList);
}

// ----------------------------------------------------------------------------
/*
 * This section maintains a list of nodes that have gravity probes
 * attached to them. Whenever nodes are deleted or added to the scene, or
 * whenever components are added or removed from nodes, we must check to see
 * which of those nodes have/had a gravity probe attached to it and update
 * our internal list accordingly.
 *
 * Unfortunately, E_COMPONENTREMOVED and E_COMPONENTADDED do not fire when a
 * parent node is removed/added containing gravity probe nodes, so we must
 * also monitor E_NODEREMOVED AND E_NODEADDED.
 */

// ----------------------------------------------------------------------------
void Gravity::OnSceneSet(Scene* scene)
{
    (void)scene;

    // do a full search for gravityProbe nodes
    gravityVectors_.Clear();
    AddGravityVectorsRecursively(node_);
    RebuildTetrahedralMesh();
}

// ----------------------------------------------------------------------------
void Gravity::AddGravityVectorsRecursively(Node* node)
{
    // Recursively retrieve all nodes that have a gravity probe component and
    // add them to our internal list of gravity probe nodes. Note that it
    // should not be possible for there to be duplicates; scene graphs can't
    // have loops.
    PODVector<Node*> gravityVectorNodesToAdd;
    node->GetChildrenWithComponent<GravityVector>(gravityVectorNodesToAdd, true);
    // Don't forget to check this node's components as well
    if(node->GetComponent<GravityVector>())
        gravityVectorNodesToAdd.Push(node);

    PODVector<Node*>::Iterator it = gravityVectorNodesToAdd.Begin();
    for(; it != gravityVectorNodesToAdd.End(); ++it)
        gravityVectors_.Push((*it)->GetComponent<GravityVector>());
}

// ----------------------------------------------------------------------------
void Gravity::RemoveGravityVectorsRecursively(Node* node)
{
    // Recursively retrieve all nodes that have a gravity probe component
    PODVector<Node*> gravityVectorNodesToRemove;
    node->GetChildrenWithComponent<GravityVector>(gravityVectorNodesToRemove, true);
    // Don't forget to check this node's components as well
    if(node->GetComponent<GravityVector>())
        gravityVectorNodesToRemove.Push(node);

    // search for found components and remove them from our internal list
    PODVector<Node*>::ConstIterator it = gravityVectorNodesToRemove.Begin();
    for(; it != gravityVectorNodesToRemove.End(); ++it)
    {
        PODVector<GravityVector*>::Iterator gravityNode = gravityVectors_.Find((*it)->GetComponent<GravityVector>());
        if(gravityNode != gravityVectors_.End())
            gravityVectors_.Erase(gravityNode);
    }
}

// ----------------------------------------------------------------------------
void Gravity::HandleComponentAdded(StringHash eventType, VariantMap& eventData)
{
    using namespace ComponentAdded;
    (void)eventType;

    // We only care about nodes that are descendants of us
    if(!node_->IsAncestorOf(static_cast<Node*>(eventData[P_NODE].GetPtr())))
        return;

    // Check if the component that was added is a gravity vector. If not, then
    // it does not concern us.
    Component* component = static_cast<Component*>(eventData[P_COMPONENT].GetPtr());
    if(component->GetType() != GravityVector::GetTypeStatic())
        return;

    gravityVectors_.Push(static_cast<GravityVector*>(component));
    RebuildTetrahedralMesh();
}

// ----------------------------------------------------------------------------
void Gravity::HandleComponentRemoved(StringHash eventType, VariantMap& eventData)
{
    using namespace ComponentRemoved;
    (void)eventType;

    // We only care about nodes that are descendants of us
    if(!node_->IsAncestorOf(static_cast<Node*>(eventData[P_NODE].GetPtr())))
        return;

    // Check if the component that was removed was a gravity vector. If not,
    // then it does not concern us.
    Component* component = static_cast<Component*>(eventData[P_COMPONENT].GetPtr());
    if(component->GetType() != GravityVector::GetTypeStatic())
        return;

    gravityVectors_.Remove(static_cast<GravityVector*>(component));
    RebuildTetrahedralMesh();
}

// ----------------------------------------------------------------------------
void Gravity::HandleNodeAdded(StringHash eventType, VariantMap& eventData)
{
    using namespace NodeAdded;
    (void)eventType;

    // We only care about nodes that are descendants of us
    Node* addedNode = static_cast<Node*>(eventData[P_NODE].GetPtr());
    if(!node_->IsAncestorOf(addedNode))
        return;

    AddGravityVectorsRecursively(addedNode);
    RebuildTetrahedralMesh();
}

// ----------------------------------------------------------------------------
void Gravity::HandleNodeRemoved(StringHash eventType, VariantMap& eventData)
{
    using namespace NodeRemoved;
    (void)eventType;

    // We only care about nodes that are descendants of us
    Node* removedNode = static_cast<Node*>(eventData[P_NODE].GetPtr());
    if(!node_->IsAncestorOf(removedNode))
        return;

    RemoveGravityVectorsRecursively(removedNode);
    RebuildTetrahedralMesh();
}

} // namespace Urho3D
